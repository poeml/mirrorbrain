# Create your views here.

from django.http import HttpResponse
from django.db.models import Sum
from django.views.decorators.cache import cache_page

from downloadstats.stats.models import Counter


#@cache_page(60*15)
def stats_csv(request, year, month, day=None):

    response = HttpResponse(mimetype='text/plain')
    #response['Content-Disposition'] = 'attachment; filename=%s%s%s.csv' % (year, month, day or '')

    import csv
    writer = csv.writer(response)
    writer.writerow(['Date', 'Product', 'Version', 'OS', 'Language', 'Country', 'Downloads'])


    products = Counter.objects.values('product').distinct()
    for product in products:

        s = Counter.objects.filter(product=product['product'])
        s = s.filter(date__year=year, date__month=month)
        if day:
            s = s.filter(date__day=day)
        s = s.values('date', 'product', 'version', 'osname', 'lang', 'country')
        s = s.annotate(counter=Sum('count'))
        s = s.order_by('date', 'product')

        for i in s:
            writer.writerow((i['date'], i['product'], i['version'], i['osname'], i['lang'], i['country'], i['counter']))

    return response





