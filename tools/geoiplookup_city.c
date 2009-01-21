/* tool to look up details about IP address or DNS name */
/* gcc -g -Wall -lGeoIP -o geoiplookup_city geoiplookup_city.c */
#include <stdio.h>
#include <GeoIP.h>
#include <GeoIPCity.h>

#define DEFAULT_GEOIPFILE "/usr/share/GeoIP/GeoLiteCity.dat"

int main(int argc, char **argv) {
	const char *geoipfilename = DEFAULT_GEOIPFILE;
	GeoIP *gip = NULL;
	GeoIPRecord *gir = NULL;
	char *name;
	const char *region_name;
	int edition, i;

	if ((argc != 2) && (argc != 4)) {
		printf("Usage: geoiplookup_city [-f custom_file] <hostname_or_ip>\n");
		return 1;
	}

	i = 1;
	while (i < argc) {
                if (strcmp(argv[i],"-f") == 0) {
                        if ((i+1) < argc){
                                i++;
                                geoipfilename = argv[i];
                        }
                } else {
			name = argv[i];
		}
                i++;
        }

	gip = GeoIP_open(geoipfilename, GEOIP_STANDARD);
	edition = GeoIP_database_edition(gip);

	if (edition == GEOIP_COUNTRY_EDITION) {
		short int country_id = GeoIP_country_id_by_name(gip, name);
		region_name = GeoIP_country_continent[country_id];
		printf("%s\n", region_name);
        } else if ((edition == GEOIP_CITY_EDITION_REV0) || 
		   (edition == GEOIP_CITY_EDITION_REV1)) {
                gir = GeoIP_record_by_name(gip, name);
                if (NULL == gir) {
                        printf("%s: IP Address not found\n", GeoIPDBDescription[edition]);
			return 1;
                } else {
                        printf("Continent: %s\n"
			       "Country:   %s\n"
			       "Region id: %s\n"
			       "Region:    %s\n"
			       "City:      %s\n"
			       "Latitude:  %f\n"
			       "Longitude: %f\n", gir->continent_code, 
			       			  gir->country_code, 
						  gir->region, 
						  GeoIP_region_name_by_code(gir->country_code, gir->region),
						  gir->city, 
						  gir->latitude, 
						  gir->longitude);
		}
	}
 
	return 0;
}

