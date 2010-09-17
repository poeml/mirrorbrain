/*
 * Copyright 2007,2008,2009,2010 Peter Poeml
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
 * Fragments of the code are copied from the GeoIP library, which is published
 * under the LGPL and Copyright 2005 MaxMind LLC. 
 *
 *
 * tool to look up city data of an IP address or DNS name
 * using the free city GeoIP database
 *
 * gcc -g -Wall -lGeoIP -o geoiplookup_city geoiplookup_city.c */
#include <stdio.h>
#include <GeoIP.h>
#include <GeoIPCity.h>

#define DEFAULT_GEOIPFILE "/var/lib/GeoIP/GeoLiteCity.dat"

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

