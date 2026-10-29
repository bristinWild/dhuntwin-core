#include "dhun/gis.h"

#include <cmath>
#include <stdexcept>

#include "dhun/catalog.h"

namespace dhun {

namespace {

constexpr double PI = 3.141592653589793;
constexpr double EARTH_RADIUS_METERS = 6378137;  // WGS 84 major axis

double toRadians(double degrees)
{
    return (degrees * PI) / 180;
}

}  // namespace

double calculateGeodesicDistanceMeters(double lat1, double lon1, double lat2, double lon2)
{
    const double dLat = toRadians(lat2 - lat1);
    const double dLon = toRadians(lon2 - lon1);

    const double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
                     std::cos(toRadians(lat1)) * std::cos(toRadians(lat2)) * std::sin(dLon / 2) *
                         std::sin(dLon / 2);

    const double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    return EARTH_RADIUS_METERS * c;
}

double calculatePathDistanceMeters(const std::vector<GeoPoint>& points)
{
    if (points.size() < 2) return 0;
    double total = 0;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        total += calculateGeodesicDistanceMeters(points[i].lat, points[i].lng, points[i + 1].lat,
                                                 points[i + 1].lng);
    }
    return total;
}

double calculateGeodesicAreaSqm(const std::vector<GeoPoint>& points)
{
    if (points.size() < 3) return 0;
    const std::size_t n = points.size();

    /* Equal-area sinusoidal projection centred on the site (the
       spherical-excess angle sum upstream computes is never used). */
    double centerLat = 0;
    for (const auto& p : points) centerLat += p.lat;
    centerLat /= static_cast<double>(n);

    const double radLat = toRadians(centerLat);
    const double mPerDegLat = 111132.92 - 559.82 * std::cos(2 * radLat);
    const double mPerDegLng = 111412.84 * std::cos(radLat);

    double area = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        const double xi = (points[i].lng - points[0].lng) * mPerDegLng;
        const double yi = (points[i].lat - points[0].lat) * mPerDegLat;
        const double xj = (points[j].lng - points[0].lng) * mPerDegLng;
        const double yj = (points[j].lat - points[0].lat) * mPerDegLat;
        area += xi * yj - xj * yi;
    }
    return std::fabs(area) / 2;
}

std::string formatDistance(double meters)
{
    if (meters < 1000) return js::toFixed(meters, 1) + " m";
    return js::toFixed(meters / 1000, 2) + " km";
}

Json formatArea(double sqm)
{
    const double acres = sqm / 4046.8564224;
    const double hectares = sqm / 10000;
    const std::string grouped = js::formatNumber(sqm, 0, 0, false);

    Json j;
    j["sqm"] = grouped + " m²";
    j["acres"] = js::toFixed(acres, 2) + " acres";
    j["hectares"] = js::toFixed(hectares, 2) + " ha";
    j["combined"] = js::toFixed(acres, 2) + " acres (" + grouped + " m²)";
    return j;
}

std::string formatCoordinatesDMS(double lat, double lng)
{
    auto dms = [](double deg, bool isLat) {
        const char* dir = isLat ? (deg >= 0 ? "N" : "S") : (deg >= 0 ? "E" : "W");
        const double absDeg = std::fabs(deg);
        const double d = std::floor(absDeg);
        const double minFloat = (absDeg - d) * 60;
        const double m = std::floor(minFloat);
        const std::string s = js::toFixed((minFloat - m) * 60, 1);
        return js::toString(d) + "° " + js::toString(m) + "' " + s + "\" " + dir;
    };
    return dms(lat, true) + ", " + dms(lng, false);
}

bool isPointInPolygon(std::pair<double, double> point, const std::vector<std::pair<double, double>>& polygon)
{
    const auto [x, y] = point;
    bool inside = false;
    const std::size_t n = polygon.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = polygon[i].first, yi = polygon[i].second;
        const double xj = polygon[j].first, yj = polygon[j].second;
        const bool intersect = ((yi > y) != (yj > y)) && x < ((xj - xi) * (y - yi)) / (yj - yi) + xi;
        if (intersect) inside = !inside;
    }
    return inside;
}

std::vector<GeoPoint> geoPoints(const Json& points)
{
    std::vector<GeoPoint> out;
    if (!points.is_array()) return out;
    for (const auto& p : points) {
        out.push_back({js::toNumber(p.value("lat", Json(nullptr))), js::toNumber(p.value("lng", Json(nullptr)))});
    }
    return out;
}

std::vector<std::pair<double, double>> xyPoints(const Json& points)
{
    std::vector<std::pair<double, double>> out;
    if (!points.is_array()) return out;
    for (const auto& p : points) {
        if (p.is_array() && p.size() >= 2) out.emplace_back(js::toNumber(p[0]), js::toNumber(p[1]));
    }
    return out;
}

std::pair<double, double> localMetersToWgs84(double dxMeters, double dyMeters)
{
    const double LAT_DATUM = 26.719597;
    const double LNG_DATUM = 75.593443;
    const double M_TO_DEG_LAT = 1 / 110816.0;
    const double M_TO_DEG_LNG = 1 / (111412.84 * std::cos((LAT_DATUM * PI) / 180));

    const double lng = LNG_DATUM + dxMeters * M_TO_DEG_LNG;
    const double lat = LAT_DATUM + dyMeters * M_TO_DEG_LAT;
    /* Number(x.toFixed(6)) */
    return {js::parseFloat(js::toFixed(lng, 6)), js::parseFloat(js::toFixed(lat, 6))};
}

const Json& gisData()
{
    return catalog::get("gis");
}

const Json& gisLayer(const std::string& exportName)
{
    const Json& data = gisData();
    if (!data.contains(exportName)) throw std::out_of_range("unknown GIS export: " + exportName);
    return data[exportName];
}

const Json& parameterRegistry()
{
    return catalog::get("parameters");
}

Json getParametersByWorkspace(const std::string& workspace)
{
    Json out = Json::array();
    for (const auto& p : parameterRegistry()) {
        for (const auto& w : p["applicableWorkspaces"]) {
            if (w == workspace) {
                out.push_back(p);
                break;
            }
        }
    }
    return out;
}

Json searchParameterRegistry(const std::string& query)
{
    const std::string q = js::trim(js::toLower(query));
    if (q.empty()) return parameterRegistry();

    Json out = Json::array();
    for (const auto& p : parameterRegistry()) {
        auto hit = [&](const char* key) {
            return p.contains(key) && p[key].is_string() && js::includes(js::toLower(p[key].get<std::string>()), q);
        };
        if (hit("id") || hit("name") || hit("description") || hit("source") || hit("evidenceClassification")) {
            out.push_back(p);
        }
    }
    return out;
}

}  // namespace dhun
