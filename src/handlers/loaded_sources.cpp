// loaded_sources.cpp — DAP "loadedSources" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <set>
#include <filesystem>

#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class loaded_sources_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
    std::string command() const override { return "loadedSources"; }

    std::string handle(const dap::request &req) override
    {
        namespace fs = std::filesystem;
        std::set<std::string> seen;
        nlohmann::json sources = nlohmann::json::array();

        for (const auto &mod : ctx_.cdb_modules())
        {
            // Collect from module file field.
            std::string file = mod.file.empty() ? mod.name : mod.file;
            if (!file.empty())
                add_source(file, seen, sources);

            // Collect unique files from line entries.
            for (const auto &ln : mod.lines)
            {
                if (!ln.file.empty())
                    add_source(ln.file, seen, sources);
            }
        }

        dap::response resp(req.seq, req.command);
        resp.success(true).result({{"sources", sources}});
        return resp.str();
    }

private:
    void add_source(const std::string &file,
                    std::set<std::string> &seen,
                    nlohmann::json &sources)
    {
        namespace fs = std::filesystem;
        auto resolved = ctx_.resolve_source_path(file);
        std::string path = resolved ? *resolved : file;

        if (!seen.insert(path).second)
            return;

        nlohmann::json src;
        src["name"] = fs::path(path).filename().string();
        src["path"] = path;
        sources.push_back(src);
    }

};

std::unique_ptr<dap::request_handler> make_loaded_sources(dbg &ctx)
{
    return make_handler<loaded_sources_handler>(ctx);
}

} // namespace handlers
