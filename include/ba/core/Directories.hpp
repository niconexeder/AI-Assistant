#pragma once

#include <string>

#include "ba/models/Directory.hpp"

namespace ba {

CityDirectory LoadCityDirectory(const std::string& path);
CategoryDirectory LoadCategoryDirectory(const std::string& path);

} // namespace ba
