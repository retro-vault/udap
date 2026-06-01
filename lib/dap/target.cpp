// target.cpp
// DAP target::run() and thread-safe event helpers.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <format>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <dap/target.h>
#include "dap.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Thread-safe event helpers
// ---------------------------------------------------------------------------

void dap::target::stopped(const std::string &reason, int thread_id)
{
    if (!event_sender_) return;
    json ev;
    ev["seq"]   = event_seq_.fetch_add(1);
    ev["type"]  = "event";
    ev["event"] = "stopped";
    ev["body"]  = {{"reason", reason}, {"threadId", thread_id},
                   {"allThreadsStopped", true}};
    event_sender_(ev.dump());
}

void dap::target::output(const std::string &text, const std::string &category)
{
    if (!event_sender_) return;
    json ev;
    ev["seq"]   = event_seq_.fetch_add(1);
    ev["type"]  = "event";
    ev["event"] = "output";
    ev["body"]  = {{"output", text}, {"category", category}};
    event_sender_(ev.dump());
}

void dap::target::terminated()
{
    if (!event_sender_) return;
    json ev;
    ev["seq"]   = event_seq_.fetch_add(1);
    ev["type"]  = "event";
    ev["event"] = "terminated";
    ev["body"]  = json::object();
    event_sender_(ev.dump());
}

void dap::target::send_event_raw(const std::string &event_json)
{
    if (event_sender_) event_sender_(event_json);
}

// ---------------------------------------------------------------------------
// target::run
// ---------------------------------------------------------------------------

void dap::target::run(std::istream &in, std::ostream &out)
{
    dap::session dispatcher;

    event_sender_ = [&dispatcher](const std::string &ev) {
        dispatcher.send_event_direct(ev);
    };

    bool launched = false;

    std::unordered_map<std::string, int> path_to_ref;
    std::unordered_map<int, std::string> ref_to_path;
    int next_src_ref = 1;

    auto assign_src_ref = [&](const std::string &path) -> int {
        auto it = path_to_ref.find(path);
        if (it != path_to_ref.end()) return it->second;
        int ref = next_src_ref++;
        path_to_ref[path] = ref;
        ref_to_path[ref]  = path;
        return ref;
    };

    std::vector<std::string> scope_names;

    auto read_disk_source = [](const std::string &path)
        -> std::optional<dap::source_info>
    {
        std::ifstream ifs(path);
        if (!ifs) return std::nullopt;
        std::ostringstream ss;
        ss << ifs.rdbuf();
        std::string mime = "text/plain";
        try {
            auto ext = fs::path(path).extension().string();
            if (ext == ".c" || ext == ".h")                         mime = "text/x-c";
            else if (ext == ".s" || ext == ".asm" || ext == ".lst") mime = "text/x-asm";
        } catch (...) {}
        return dap::source_info{ss.str(), mime};
    };

    dispatcher.register_typed_handler<dap::initialize_request>("initialize",
        [&](const dap::initialize_request &r) {
            json ev;
            ev["seq"]   = event_seq_.fetch_add(1);
            ev["type"]  = "event";
            ev["event"] = "initialized";
            ev["body"]  = json::object();
            dispatcher.send_event_direct(ev.dump());
            return dap::response(r.seq, r.command)
                .success(true)
                .result({
                    {"supportsConfigurationDoneRequest",   true},
                    {"supportsBreakpointLocationsRequest", true},
                    {"supportsInstructionBreakpoints",     true},
                    {"supportsLoadedSourcesRequest",       true},
                    {"supportsDisassembleRequest",         true},
                    {"supportsFunctionBreakpoints",        true},
                    {"supportsStepBack",                   true},
                    {"supportsRestartFrame",               false},
                    {"supportsEvaluateForHovers",          true},
                    {"supportsSetVariable",                false},
                    {"supportsTerminateDebuggee",          false},
                    {"supportsMemoryReferences",           true},
                    {"supportsReadMemoryRequest",          true}})
                .str();
        });

    dispatcher.register_typed_handler<dap::launch_request>("launch",
        [&](const dap::launch_request &r) {
            dap::launch_args args;
            args.program     = r.program;
            args.cdb_file    = r.cdb_file;
            args.map_file    = r.map_file;
            args.source_root = r.arguments.value("sourceRoot", "");
            for (const char *key : {"sourceRoots", "includeRoots"})
                if (r.arguments.contains(key) && r.arguments[key].is_array())
                    for (const auto &item : r.arguments[key])
                        if (item.is_string())
                            args.source_roots.push_back(item.get<std::string>());
            if (r.arguments.contains("startAddress")) {
                const auto &v = r.arguments["startAddress"];
                try {
                    uint32_t raw = v.is_string()
                        ? static_cast<uint32_t>(std::stoul(v.get<std::string>(), nullptr, 0))
                        : v.get<uint32_t>();
                    args.start_address = static_cast<uint16_t>(raw & 0xFFFF);
                } catch (...) {}
            }
            launched = launch(args);
            return dap::response(r.seq, r.command).success(true).result({}).str();
        });

    dispatcher.register_typed_handler<dap::configuration_done_request>("configurationDone",
        [&](const dap::configuration_done_request &r) {
            std::string resp = dap::response(r.seq, r.command).success(true).result({}).str();
            if (launched) stopped("entry");
            return resp;
        });

    dispatcher.register_handler("disconnect",
        [&](const std::string &json_text) {
            auto req = dap::request::parse(json_text);
            launched = false;
            disconnect();
            return dap::response(req.seq, req.command).success(true).result({}).str();
        });

    dispatcher.register_typed_handler<dap::threads_request>("threads",
        [&](const dap::threads_request &r) {
            return dap::response(r.seq, r.command)
                .success(true)
                .result({{"threads", {{{"id", 1}, {"name", "main"}}}}})
                .str();
        });

    dispatcher.register_typed_handler<dap::stack_trace_request>("stackTrace",
        [&](const dap::stack_trace_request &r) {
            auto frames = get_stack();
            json jframes = json::array();
            int frame_id = 1;
            for (const auto &f : frames) {
                json frame;
                frame["id"]                   = frame_id++;
                frame["name"]                 = f.name;
                frame["memoryReference"]      = f.address;
                frame["instructionReference"] = f.address;
                frame["line"]                 = f.line;
                frame["column"]               = 1;
                if (!f.source_path.empty()) {
                    json src;
                    src["name"]             = fs::path(f.source_path).filename().string();
                    src["path"]             = f.source_path;
                    src["presentationHint"] = "normal";
                    src["sourceReference"]  = fs::exists(f.source_path)
                                              ? 0 : assign_src_ref(f.source_path);
                    frame["source"] = src;
                }
                jframes.push_back(frame);
            }
            return dap::response(r.seq, r.command)
                .success(true)
                .result({{"stackFrames", jframes},
                         {"totalFrames", static_cast<int>(frames.size())}})
                .str();
        });

    dispatcher.register_typed_handler<dap::scopes_request>("scopes",
        [&](const dap::scopes_request &r) {
            scope_names.clear();
            auto scopes = get_scopes();
            json jscopes = json::array();
            for (size_t i = 0; i < scopes.size(); ++i) {
                scope_names.push_back(scopes[i].name);
                jscopes.push_back({{"name",               scopes[i].name},
                                   {"variablesReference", static_cast<int>(i) + 1},
                                   {"presentationHint",   scopes[i].hint},
                                   {"expensive",          false}});
            }
            return dap::response(r.seq, r.command)
                .success(true).result({{"scopes", jscopes}}).str();
        });

    dispatcher.register_typed_handler<dap::variables_request>("variables",
        [&](const dap::variables_request &r) {
            json jvars = json::array();
            int idx = r.variables_reference - 1;
            if (idx >= 0 && idx < static_cast<int>(scope_names.size())) {
                for (const auto &v : get_variables(scope_names[idx])) {
                    json jv;
                    jv["name"]               = v.name;
                    jv["value"]              = v.value;
                    jv["variablesReference"] = 0;
                    if (!v.type.empty())    jv["type"]            = v.type;
                    if (v.memory_reference) jv["memoryReference"] = *v.memory_reference;
                    jvars.push_back(jv);
                }
            }
            return dap::response(r.seq, r.command)
                .success(true).result({{"variables", jvars}}).str();
        });

    dispatcher.register_typed_handler<dap::continue_request>("continue",
        [&](const dap::continue_request &r) {
            resume();
            return dap::response(r.seq, r.command)
                .success(true).result({{"allThreadsContinued", true}}).str();
        });

    dispatcher.register_typed_handler<dap::next_request>("next",
        [&](const dap::next_request &r) {
            step();
            std::string resp = dap::response(r.seq, r.command)
                .success(true).result({{"allThreadsContinued", false}}).str();
            stopped("step");
            return resp;
        });

    dispatcher.register_typed_handler<dap::step_in_request>("stepIn",
        [&](const dap::step_in_request &r) {
            step_in();
            std::string resp = dap::response(r.seq, r.command).success(true).str();
            stopped("step");
            return resp;
        });

    dispatcher.register_typed_handler<dap::step_out_request>("stepOut",
        [&](const dap::step_out_request &r) {
            step_out();
            std::string resp = dap::response(r.seq, r.command).success(true).str();
            stopped("step");
            return resp;
        });

    dispatcher.register_typed_handler<dap::step_back_request>("stepBack",
        [&](const dap::step_back_request &r) {
            step_back();
            std::string resp = dap::response(r.seq, r.command).success(true).str();
            stopped("step");
            return resp;
        });

    dispatcher.register_handler("pause",
        [&](const std::string &json_text) {
            auto req = dap::request::parse(json_text);
            pause();
            return dap::response(req.seq, req.command).success(true).str();
        });

    dispatcher.register_typed_handler<dap::set_breakpoints_request>("setBreakpoints",
        [&](const dap::set_breakpoints_request &r) {
            json jbps = json::array();
            for (const auto &bp : set_source_breakpoints(r.source_path, r.lines)) {
                json jbp = {{"verified", bp.verified}, {"line", bp.line}};
                if (!bp.message.empty())      jbp["message"]              = bp.message;
                if (bp.instruction_reference) jbp["instructionReference"] = *bp.instruction_reference;
                jbps.push_back(jbp);
            }
            return dap::response(r.seq, r.command)
                .success(true).result({{"breakpoints", jbps}}).str();
        });

    dispatcher.register_typed_handler<dap::set_function_breakpoints_request>(
        "setFunctionBreakpoints",
        [&](const dap::set_function_breakpoints_request &r) {
            std::vector<std::string> names;
            for (const auto &bp : r.breakpoints) names.push_back(bp.value("name", ""));
            json jbps = json::array();
            for (const auto &bp : set_function_breakpoints(names)) {
                json jbp = {{"verified", bp.verified}};
                if (!bp.message.empty())      jbp["message"]              = bp.message;
                if (bp.instruction_reference) jbp["instructionReference"] = *bp.instruction_reference;
                jbps.push_back(jbp);
            }
            return dap::response(r.seq, r.command)
                .success(true).result({{"breakpoints", jbps}}).str();
        });

    dispatcher.register_typed_handler<dap::set_instruction_breakpoints_request>(
        "setInstructionBreakpoints",
        [&](const dap::set_instruction_breakpoints_request &r) {
            std::vector<std::string> refs;
            for (const auto &bp : r.breakpoints)
                if (bp.contains("instructionReference"))
                    refs.push_back(bp["instructionReference"].get<std::string>());
            json jbps = json::array();
            for (const auto &bp : set_instruction_breakpoints(refs)) {
                json jbp = {{"verified", bp.verified}};
                if (bp.instruction_reference) jbp["instructionReference"] = *bp.instruction_reference;
                jbps.push_back(jbp);
            }
            return dap::response(r.seq, r.command)
                .success(true).result({{"breakpoints", jbps}}).str();
        });

    dispatcher.register_handler("setExceptionBreakpoints",
        [](const std::string &json_text) {
            auto req = dap::request::parse(json_text);
            return dap::response(req.seq, req.command).success(true).result({}).str();
        });

    dispatcher.register_typed_handler<dap::breakpoint_locations_request>(
        "breakpointLocations",
        [&](const dap::breakpoint_locations_request &r) {
            std::string path;
            if (r.source.contains("path"))      path = r.source["path"].get<std::string>();
            else if (r.source.contains("name")) path = r.source["name"].get<std::string>();
            json jlocs = json::array();
            for (const auto &loc : get_breakpoint_locations(path, r.line, r.end_line))
                jlocs.push_back({{"line", loc.line}});
            return dap::response(r.seq, r.command)
                .success(true).result({{"breakpoints", jlocs}}).str();
        });

    dispatcher.register_typed_handler<dap::source_request>("source",
        [&](const dap::source_request &r) {
            std::string path;
            if (r.source_reference > 0) {
                auto it = ref_to_path.find(r.source_reference);
                if (it != ref_to_path.end()) path = it->second;
            } else {
                if (r.arguments.contains("source") &&
                    r.arguments["source"].contains("path"))
                    path = r.arguments["source"]["path"].get<std::string>();
            }
            if (!path.empty()) {
                auto info = get_source(path);
                if (!info) info = read_disk_source(path);
                if (info)
                    return dap::response(r.seq, r.command)
                        .success(true)
                        .result({{"content", info->content}, {"mimeType", info->mime_type}})
                        .str();
            }
            return dap::response(r.seq, r.command)
                .success(false).message("Unknown source").str();
        });

    dispatcher.register_typed_handler<dap::read_memory_request>("readMemory",
        [&](const dap::read_memory_request &r) {
            uint32_t start = (static_cast<uint32_t>(r.memory_reference) + r.offset) & 0xFFFF;
            auto bytes = read_memory(static_cast<uint16_t>(start), std::max(0, r.count));
            static const char b64[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            const auto *data = bytes.data();
            size_t len = bytes.size();
            std::string encoded;
            encoded.reserve(((len + 2) / 3) * 4);
            for (size_t i = 0; i < len; i += 3) {
                uint32_t n = static_cast<uint32_t>(data[i]) << 16;
                if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
                if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
                encoded.push_back(b64[(n >> 18) & 0x3F]);
                encoded.push_back(b64[(n >> 12) & 0x3F]);
                encoded.push_back((i + 1 < len) ? b64[(n >> 6) & 0x3F] : '=');
                encoded.push_back((i + 2 < len) ? b64[n & 0x3F] : '=');
            }
            return dap::response(r.seq, r.command)
                .success(true)
                .result({{"address",         std::format("0x{:04X}", start)},
                         {"data",            encoded},
                         {"unreadableBytes", 0}})
                .str();
        });

    dispatcher.register_typed_handler<dap::disassemble_request>("disassemble",
        [&](const dap::disassemble_request &r) {
            json jinstrs = json::array();
            for (const auto &i : disassemble(r.memory_reference, r.offset,
                                              r.instruction_offset, r.instruction_count)) {
                json ji;
                ji["address"]          = i.address;
                ji["instructionBytes"] = i.instruction_bytes;
                ji["instruction"]      = i.instruction;
                if (i.symbol)      ji["symbol"]   = *i.symbol;
                if (i.source_path) ji["location"] = {{"path", *i.source_path}};
                if (i.source_line) ji["line"]     = *i.source_line;
                jinstrs.push_back(ji);
            }
            return dap::response(r.seq, r.command)
                .success(true).result({{"instructions", jinstrs}}).str();
        });

    dispatcher.register_typed_handler<dap::evaluate_request>("evaluate",
        [&](const dap::evaluate_request &r) {
            auto res = evaluate(r.expression);
            if (!res.success)
                return dap::response(r.seq, r.command)
                    .success(false).message(res.error_message).str();
            json body;
            body["result"]             = res.result;
            body["variablesReference"] = 0;
            if (res.memory_reference)  body["memoryReference"] = *res.memory_reference;
            return dap::response(r.seq, r.command).success(true).result(body).str();
        });

    dispatcher.register_handler("loadedSources",
        [&](const std::string &json_text) {
            auto req = dap::request::parse(json_text);
            json jsrcs = json::array();
            for (const auto &s : get_loaded_sources())
                jsrcs.push_back({{"name", s.name}, {"path", s.path}});
            return dap::response(req.seq, req.command)
                .success(true).result({{"sources", jsrcs}}).str();
        });

    dispatcher.run(in, out);
    event_sender_ = nullptr;
}
