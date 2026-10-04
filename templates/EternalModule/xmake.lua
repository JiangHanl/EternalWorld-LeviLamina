-- Include this target from a configured Eternal root project.
target("EternalExample")
    set_kind("shared")
    set_languages("cxx20")
    set_runtimes("MD")
    add_defines("ETERNAL_MODULE_BUILD")
    add_includedirs(path.join(os.projectdir(), "sdk/EternalSDK/include"))
    add_packages("nlohmann_json")
    add_files("Module.cpp")
    -- This is an Eternal module: do not add the LeviLamina NativeMod rule,
    -- LL_REGISTER_MOD, or an independent plugin manifest.
