import("core.base.json")
import("core.package.package", {alias = "pkg"})
import("private.action.require.impl.repository", {alias = "repos"})

function main(project_root)
    local lock = json.loadfile(path.join(project_root, "tools/build-lock.json"))
    local expected_root = path.absolute(lock.recipe_repository.path, project_root)
    for _, name in ipairs({"expected-lite", "libhat", "levilamina", "leveldb"}) do
        local dir, repo = repos.packagedir(name)
        assert(dir and repo, "recipe not found: " .. name)
        assert(path.absolute(dir):startswith(expected_root .. path.sep()), "unlocked recipe selected: " .. name .. " from " .. dir)
        local p = assert(pkg.load_from_repository(name, dir, {plat = "windows", arch = "x64"}))
        if name == "expected-lite" then
            assert(p:get("versions")[lock.expected_lite.version] == lock.expected_lite.commit, "wrong expected-lite source")
        elseif name == "libhat" then
            assert(p:get("versions")[lock.libhat_archive.version] == lock.libhat_archive.sha256, "wrong libhat archive hash")
            local url = table.wrap(p:get("urls"))[1]
            assert(url == lock.libhat_archive.url, "wrong libhat download URL")
            assert(p:extraconf("urls", url, "filename") == "libhat-0.4.0.zip", "libhat ZIP extraction filename missing")
        end
        print("PASS recipe %s: %s", name, repo:name())
    end
    print("PASS expected-lite source %s; libhat source %s / SHA256 %s", lock.expected_lite.commit, lock.libhat_archive.commit, lock.libhat_archive.sha256)
end
