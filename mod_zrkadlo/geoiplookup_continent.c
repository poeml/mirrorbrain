/* tool to look up the continent of an IP address or DNS name */
/* gcc -g -Wall -lGeoIP -o geoiplookup_continent geoiplookup_continent.c */
#include <stdio.h>
#include <GeoIP.h>

#define DEFAULT_GEOIPFILE "/usr/share/GeoIP/GeoIP.dat"

int main(int argc, char **argv) {
	const char *geoipfilename = DEFAULT_GEOIPFILE;
	GeoIP *gip = NULL;
	const char *name;

	if (argc != 2) {
		printf("Usage: geoiplookup_continent <hostname_or_ip>\n");
		return 1;
	}
	name = argv[1];
	gip = GeoIP_open(geoipfilename, GEOIP_STANDARD);

	short int country_id = GeoIP_country_id_by_name(gip, name);
	const char* continent_code = GeoIP_country_continent[country_id];

	printf("%s\n", continent_code);

	return 0;
}

