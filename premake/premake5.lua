if (_ACTION == nil) then
  return
end

local RD = path.getabsolute("../") .. "/"

workspace("ac-examples")
startproject("00_triangle")

configurations({
  "release",
  "debug",
  "dist"
})

include(RD .. "../ac/premake/")
include(RD .. "../ac-tools/imgui")

project("ac-examples-shaders")
  kind("Utility")

  ac_compile_shader("../00_triangle/main.acsl", "vs fs")
  ac_compile_shader("../01_cube/main.acsl", "vs fs")
  ac_compile_shader("../02_model/main.acsl", "vs fs")
  ac_compile_shader("../03_dynamic_geometry/main.acsl", "vs fs")
  ac_compile_shader("../05_pbr/brdf.acsl", "cs")
  ac_compile_shader("../05_pbr/eq_to_cube.acsl", "cs")
  ac_compile_shader("../05_pbr/irradiance.acsl", "cs")
  ac_compile_shader("../05_pbr/specular.acsl", "cs")
  ac_compile_shader("../05_pbr/main.acsl", "vs fs")
  ac_compile_shader("../06_shadow_mapping/shadow_mapping_depth.acsl", "vs")
  ac_compile_shader("../06_shadow_mapping/shadow_mapping.acsl", "vs fs")
  -- ac_compile_shader("../08_rayquery/main.acsl", "vs fs --permutations 2")
  -- ac_compile_shader("../09_raytracing/main.acsl", "raygen closest_hit miss")
  -- ac_compile_shader("../10_mesh/main.acsl", "mesh fs")

project("common")
  kind("StaticLib")

  dependson({
    "ac-examples-shaders",
    "ac-imgui"
  })

  warnings("Off")

  externalincludedirs({
    RD .. "../ac/include",
    RD .. "../ac-tools/imgui",
    RD .. "external",
  })

  files({
    RD .. "external/tinygltf/*.cc",
    RD .. "external/tinyobjloader/*.cc"
  })

function copy_file(file)
  files({ RD .. file })

  if (_ACTION == "xcode4") then
    xcodebuildresources({ file })
    return
  end

  filter({ "files:../" .. file })
    local dst = "%{cfg.targetdir}/%{file.basename}%{file.extension}"

    buildinputs({ RD .. file })
    buildcommands({
      "{COPYFILE} %{file.relpath} " .. dst
    })
    buildoutputs({ dst })

  filter({})
end

function setup_example(name)
  kind("WindowedApp")
  warnings("Off")

  dependson({
    "common",
  })
  links({
    "common",
    "ac-imgui"
  })

  externalincludedirs({
    RD .. "../ac/include",
    RD .. "../ac-tools/imgui",
    RD .. "external",
    RD .. "external/glm"
  })
end

project("00-triangle")
  setup_example("00-triangle")

  files({ RD .. "00_triangle/main.cpp" })

project("01-cube")
  setup_example("01-cube")

  files({ RD .. "01_cube/main.cpp" })

project("02-model")
  setup_example("02-model")

  files({ RD .. "02_model/main.cpp" })

  copy_file("data/viking_room.model")
  copy_file("data/viking_room.png")

project("03-dynamic-geometry")
  setup_example("03-dynamic-geometry")

  files({ RD .. "03_dynamic_geometry/main.cpp" })

project("04-gui")
  setup_example("04-gui")

  files({ RD .. "04_gui/main.cpp" })

project("05-pbr")
  setup_example("05-pbr")

  files({
    RD .. "05_pbr/main.cpp",
    RD .. "05_pbr/model.cpp",
    RD .. "05_pbr/model.hpp",
    RD .. "05_pbr/pbr_maps.cpp",
    RD .. "05_pbr/pbr_maps.hpp"
  })

  copy_file("data/BrainStem.glb")
  copy_file("data/clouds.hdr")

project("06-shadow-mapping")
  setup_example("06-shadow-mapping")

  files({ RD .. "06_shadow_mapping/main.cpp" })

project("07-input")
  setup_example("07-input")

  files({ RD .. "07_input/main.cpp" })


-- project("08-rayquery")
--   kind("Utility")
--   filter({ "platforms:not ps* or xbox-one"})
--     setup_example("08-rayquery")

--     files({ RD .. "08_rayquery/main.cpp" })
--   filter({})

-- project("09_raytracing")
-- 	setup_example("09_raytracing")

-- 	files({ RD .. "09_raytracing/main.cpp" })

-- project("10-mesh")
--   kind("Utility")
--   filter({ "platforms:windows or linux" })
--     setup_example("10-mesh")
--     files({ RD .. "10_mesh/main.cpp" })
--   filter({})
