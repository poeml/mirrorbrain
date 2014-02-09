from django.http import HttpResponse
from django.db.models import Sum

from downloadstats.stats.models import Counter


def stats_csv(request, year, month, day=None, by_country=True):
    import csv

    response = HttpResponse(mimetype='text/plain')
    #response['Content-Disposition'] = 'attachment; filename=%s%s%s.csv' % (year, month, day or '')

    writer = csv.writer(response)
    if by_country:
        writer.writerow(['Date', 'Product', 'Version', 'OS', 'Language', 'Country', 'Downloads'])
    else:
        writer.writerow(['Date', 'Product', 'Version', 'OS', 'Language', 'Downloads'])


    products = [ i['product'] for i in Counter.objects.values('product').distinct() ]

    for product in products:

        s = Counter.objects.filter(product=product)
        s = s.filter(date__year=year, date__month=month)
        if day:
            s = s.filter(date__day=day)
        if by_country:
            s = s.values('date', 'product', 'version', 'osname', 'lang', 'country')
        else:
            s = s.values('date', 'product', 'version', 'osname', 'lang')
        s = s.annotate(counter=Sum('count'))
        s = s.order_by('date', 'product')

        for i in s:
            if by_country:
                writer.writerow((i['date'], i['product'], i['version'], i['osname'], i['lang'], i['country'], i['counter']))
            else:
                writer.writerow((i['date'], i['product'], i['version'], i['osname'], i['lang'], i['counter']))

    return response






