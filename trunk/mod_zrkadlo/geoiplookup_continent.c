/* tool to look up the continent of an IP address or DNS name */
/* gcc -g -Wall -lGeoIP -o geoiplookup_continent geoiplookup_continent.c */
#include <stdio.h>
#include <GeoIP.h>
#include <GeoIPCity.h>

#define DEFAULT_GEOIPFILE "/usr/share/GeoIP/GeoIP.dat"

int main(int argc, char **argv) {
	const char *geoipfilename = DEFAULT_GEOIPFILE;
	GeoIP *gip = NULL;
	GeoIPRecord *gir = NULL;
	char *name;
	const char *region_name;
	int edition, i;

	if ((argc != 2) && (argc != 4)) {
		printf("Usage: geoiplookup_continent [-f custom_file] <hostname_or_ip>\n");
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
                        /* printf("%s: IP Address not found\n", GeoIPDBDescription[edition]); */
                        printf("--\n");
			return 1;
                } else {
                        printf("%s\n", gir->continent_code);
		}
	}
 
	return 0;
}

