#include "geo.h"

#include <math.h>

float haversine_distance(float lat1, float lon1, float lat2, float lon2) {
    float R = 6371000; // Earth radius in meters
    float phi1 = lat1 * M_PI / 180;
    float phi2 = lat2 * M_PI / 180;
    float deltaPhi = (lat2 - lat1) * M_PI / 180;
    float deltaLambda = (lon2 - lon1) * M_PI / 180;

    float a = sin(deltaPhi / 2) * sin(deltaPhi / 2) +
              cos(phi1) * cos(phi2) *
              sin(deltaLambda / 2) * sin(deltaLambda / 2);
    float c = 2 * atan2(sqrt(a), sqrt(1 - a));

    return R * c;
}
