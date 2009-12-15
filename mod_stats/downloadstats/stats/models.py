from django.db import models


class Counter(models.Model):

    # I'm undecided whether to settle for a "normal" date field, or optimize as
    # described in the proposal.
    #year = models.PositiveSmallIntegerField()
    #day = models.PositiveSmallIntegerField()
    date = models.DateField()
    
    product = models.CharField(max_length=512)

    # all these should probably called attr1, attr2, attr3, ...
    osname = models.CharField(max_length=512)
    version = models.CharField(max_length=32)
    lang = models.CharField(max_length=32)
    country = models.CharField(max_length=2)

    count = models.IntegerField(default=1)

    def __unicode__(self):
        return u'%s / %s / %s / %s / %s' % (self.product, 
                                        self.osname,
                                        self.version,
                                        self.lang,
                                        self.country)

