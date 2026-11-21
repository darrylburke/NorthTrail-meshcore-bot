// Host tests for BotGeo.h (distance, bearing, sun times, moon phase).
#include <gtest/gtest.h>
#include <cmath>
#include "helpers/bot/BotGeo.h"

// Toronto City Hall to Parliament Hill, Ottawa. Haversine with R = 6371 km gives 352.03 km;
// the initial bearing is 54.7 degrees, which rounds to NE.
TEST(Geo, TorontoToOttawa)       { double d=botDistanceKm(43.6534,-79.3841,45.4236,-75.7009); EXPECT_NEAR(d,352.03,0.5); EXPECT_STREQ(botBearing8(43.6534,-79.3841,45.4236,-75.7009),"NE"); EXPECT_STREQ(botBearing8(45.4236,-75.7009,43.6534,-79.3841),"SW"); }
TEST(Geo, SamePointZero)         { EXPECT_NEAR(botDistanceKm(10,20,10,20),0.0,1e-6); }
TEST(Geo, BearingEastWest)       { EXPECT_STREQ(botBearing8(10,19,10,20),"E"); EXPECT_STREQ(botBearing8(10,20,10,19),"W"); }
TEST(Sun, TorontoWinterSolstice) { uint32_t r,s; ASSERT_TRUE(botSunTimes(43.65,-79.38,1797854400u,r,s)); /* 2026-12-21 12:00 UTC */ EXPECT_NEAR((double)r,1797857280.0,300.0); /* 12:48 UTC */ EXPECT_NEAR((double)s,1797889380.0,300.0); /* 21:43 UTC */ }
TEST(Sun, PolarNight)            { uint32_t r,s; EXPECT_FALSE(botSunTimes(80.0,0.0,1797854400u,r,s)); }
TEST(Moon, KnownPhases)          { EXPECT_STREQ(botMoonPhase(947182440u),"New"); EXPECT_STREQ(botMoonPhase(947182440u+1275721u),"Full"); }

// Extra edge cases.
TEST(Geo, AntimeridianShortWay)  { double d=botDistanceKm(0,179.5,0,-179.5); EXPECT_NEAR(d,111.19,0.5); }
TEST(Geo, QuarterEarth)          { EXPECT_NEAR(botDistanceKm(0,0,90,0),6371.0*M_PI/2,0.01); }
TEST(Geo, BearingAllEight) {
  EXPECT_STREQ(botBearing8(10,20,11,20),"N");
  EXPECT_STREQ(botBearing8(10,20,11,21),"NE");
  EXPECT_STREQ(botBearing8(10,20,10,21),"E");
  EXPECT_STREQ(botBearing8(10,20,9,21),"SE");
  EXPECT_STREQ(botBearing8(10,20,9,20),"S");
  EXPECT_STREQ(botBearing8(10,20,9,19),"SW");
  EXPECT_STREQ(botBearing8(10,20,10,19),"W");
  EXPECT_STREQ(botBearing8(10,20,11,19),"NW");
}
TEST(Geo, BearingAcrossAntimeridian) { EXPECT_STREQ(botBearing8(0,179.5,0,-179.5),"E"); EXPECT_STREQ(botBearing8(0,-179.5,0,179.5),"W"); }
TEST(Geo, BearingSamePointIsN)   { EXPECT_STREQ(botBearing8(10,20,10,20),"N"); }

// Independent reference of the NOAA equations (minutes after 00:00 UTC), for the cases without almanac values.
static void refSun(double lat, double lon, int doy, double& rise_min, double& set_min) {
  double g = 2*M_PI/365.0*(doy-1);
  double eq = 229.18*(0.000075+0.001868*cos(g)-0.032077*sin(g)-0.014615*cos(2*g)-0.040849*sin(2*g));
  double dc = 0.006918-0.399912*cos(g)+0.070257*sin(g)-0.006758*cos(2*g)+0.000907*sin(2*g)-0.002697*cos(3*g)+0.00148*sin(3*g);
  double la = lat*M_PI/180;
  double ha = acos(cos(90.833*M_PI/180)/(cos(la)*cos(dc))-tan(la)*tan(dc))*180/M_PI;
  rise_min = 720-4*(lon+ha)-eq; set_min = 720-4*(lon-ha)-eq;
}
TEST(Sun, WestLongitudeSetPastMidnightUtc) {
  uint32_t t=1782648000u; /* 2026-06-28 12:00 UTC, day start 1782604800 */
  uint32_t r,s; ASSERT_TRUE(botSunTimes(43.65,-79.38,t,r,s));
  EXPECT_GT(s, 1782604800u+86400u);   // sunset after 24:00 UTC, not wrapped
  EXPECT_LT(r, s);
  double rm,sm; refSun(43.65,-79.38,179,rm,sm);
  EXPECT_NEAR((double)r,1782604800.0+rm*60,60.0); EXPECT_NEAR((double)s,1782604800.0+sm*60,60.0);
}
TEST(Sun, SouthernHemisphereSydney) {
  uint32_t t=1797854400u; uint32_t r,s; ASSERT_TRUE(botSunTimes(-33.87,151.21,t,r,s));
  uint32_t d0=t-t%86400u; double rm,sm; refSun(-33.87,151.21,355,rm,sm);
  EXPECT_NEAR((double)r,(double)d0+rm*60,60.0); EXPECT_NEAR((double)s,(double)d0+sm*60,60.0);
  EXPECT_LT(r, d0);   // eastern longitude: sunrise is before 00:00 UTC of the day
  EXPECT_GT(s-r, 13u*3600u);  // long summer day
}
TEST(Sun, EasternSunriseBeforeEpochClampsToZero) { uint32_t r=123,s=123; ASSERT_TRUE(botSunTimes(-33.87,151.21,100u,r,s)); EXPECT_EQ(r,0u); EXPECT_GT(s,0u); }  // 1970-01-01, Sydney sunrise ~ -5 h UTC (about 18:50 UTC the day before)
TEST(Sun, MidnightSunNorth)      { uint32_t r,s; EXPECT_FALSE(botSunTimes(80.0,0.0,1782648000u,r,s)); }  // June: polar day
TEST(Sun, SameDayForAnyTimeOfDay) { uint32_t r1,s1,r2,s2; ASSERT_TRUE(botSunTimes(43.65,-79.38,1797811200u,r1,s1)); ASSERT_TRUE(botSunTimes(43.65,-79.38,1797811200u+86399u,r2,s2)); EXPECT_EQ(r1,r2); EXPECT_EQ(s1,s2); }
TEST(Moon, QuartersAndGibbous) {
  const double P=29.530588853*86400.0; uint32_t base=947182440u;
  EXPECT_STREQ(botMoonPhase(base+(uint32_t)(P*0.125)),"Waxing Crescent");
  EXPECT_STREQ(botMoonPhase(base+(uint32_t)(P*0.25)),"First Quarter");
  EXPECT_STREQ(botMoonPhase(base+(uint32_t)(P*0.375)),"Waxing Gibbous");
  EXPECT_STREQ(botMoonPhase(base+(uint32_t)(P*0.625)),"Waning Gibbous");
  EXPECT_STREQ(botMoonPhase(base+(uint32_t)(P*0.75)),"Last Quarter");
  EXPECT_STREQ(botMoonPhase(base+(uint32_t)(P*0.875)),"Waning Crescent");
  EXPECT_STREQ(botMoonPhase(base+(uint32_t)(P*0.97)),"New");   // last sixteenth belongs to New
  EXPECT_STREQ(botMoonPhase(base+(uint32_t)(P*1.03)),"New");
}
TEST(Moon, BeforeReferenceDate) {
  EXPECT_STREQ(botMoonPhase(947182440u-1275721u),"Full");     // half a cycle earlier
  EXPECT_STREQ(botMoonPhase(0u),"Last Quarter");           // 1970-01-01: age ~22.6 days
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
