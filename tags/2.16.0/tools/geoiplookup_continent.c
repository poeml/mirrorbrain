/*
 * Copyright 2007,2008,2009,2010,2011,2012 Peter Poeml
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 *
 *
 *
 * handy tool to look up the continent of an IP address or DNS name
 *
 * It should be possible to compile this as such:
 * gcc -g -Wall -lGeoIP -o geoiplookup_continent geoiplookup_continent.c */

#include <stdio.h>
#include <GeoIP.h>
#include <GeoIPCity.h>

#define DEFAULT_GEOIPFILE "/var/lib/GeoIP/GeoIP.dat"

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

