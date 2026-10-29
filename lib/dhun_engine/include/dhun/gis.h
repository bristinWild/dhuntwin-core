#pragma once

/* ============================================================
   GIS AND PARAMETER REGISTER
   Ports of gis/spatialUtils.ts, gis/dhunGeoData.ts (helpers and
   layers), gis/gisMetadata.ts and data/parameterRegistry.ts.
   ============================================================ */

#include <string>
#include <utility>
#include <vector>

#include "dhun/js.h"

namespace dhun {

struct GeoPoint {
    double lat = 0;
    double lng = 0;
};

/* ---------- spatialUtils.ts ---------- */

double calculateGeodesicDistanceMeters(double lat1, double lon1, double lat2, double lon2);
double calculatePathDistanceMeters(const std::vector<GeoPoint>& points);
double calculateGeodesicAreaSqm(const std::vector<GeoPoint>& points);
std::string formatDistance(double meters);
/* { sqm, acres, hectares, combined } — numbers grouped en-US, as the
   upstream default-locale call renders on an en-US host */
Json formatArea(double sqm);
std::string formatCoordinatesDMS(double lat, double lng);
bool isPointInPolygon(std::pair<double, double> point, const std::vector<std::pair<double, double>>& polygon);

/* JSON adapters: [{lat,lng}] and [[x,y]] */
std::vector<GeoPoint> geoPoints(const Json& points);
std::vector<std::pair<double, double>> xyPoints(const Json& points);

/* ---------- dhunGeoData.ts ---------- */

/* [lng, lat] */
std::pair<double, double> localMetersToWgs84(double dxMeters, double dyMeters);

/* Every exported layer and table of dhunGeoData.ts / gisMetadata.ts,
   keyed by the upstream export name (PROPERTY_BOUNDARY_500AC,
   DHUN_SITE_CENTER, GIS_LAYERS_REGISTRY, ...). */
const Json& gisData();
const Json& gisLayer(const std::string& exportName);

/* ---------- parameterRegistry.ts ---------- */

const Json& parameterRegistry();
Json getParametersByWorkspace(const std::string& workspace);
Json searchParameterRegistry(const std::string& query);

}  // namespace dhun
