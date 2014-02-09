from django.contrib import admin

from downloadstats.stats.models import Counter


class CounterAdmin(admin.ModelAdmin):

    list_display = ('product', 'osname', 'version', 'lang', 'country', 'date', 'count')
    ordering = ('product', 'osname', 'version', 'lang', 'country')

    search_fields = ('product', 'osname')
    list_filter = ('date', 'product', 'osname', 'version', 'lang', 'country')

admin.site.register(Counter, CounterAdmin)

