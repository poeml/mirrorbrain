from django.conf.urls.defaults import *

urlpatterns = patterns('downloadstats.stats.views',

                       (r'^csv/(?P<year>\d{4})(?P<month>\d{2})(?P<day>\d{2})\.csv$', 'stats_csv'),
                       (r'^csv/(?P<year>\d{4})(?P<month>\d{2})\.csv$',               'stats_csv'),

                       (r'^csv/all-countries/(?P<year>\d{4})(?P<month>\d{2})(?P<day>\d{2})\.csv$', 'stats_csv', {'by_country': False}),
                       (r'^csv/all-countries/(?P<year>\d{4})(?P<month>\d{2})\.csv$',               'stats_csv', {'by_country': False}),
                       )
