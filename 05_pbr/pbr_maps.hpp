#pragma once

#include <string>
#include <ac/ac.h>

struct PBRMaps {
  ac_image brdf;
  ac_image environment;
  ac_image irradiance;
  ac_image specular;
};

ac_result
compute_pbr_maps(ac_device device, std::string filename, PBRMaps* maps);
