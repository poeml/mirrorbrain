from django.db import models


class Counter(models.Model):

    # I'm undecided whether to settle for a "normal" date field, or optimize as
    # described in the proposal.
    #year = models.PositiveSmallIntegerField()
    #day = models.PositiveSmallIntegerField()
    date = models.DateField(db_index=True)
    
    product = models.CharField(max_length=256, db_index=True)

    # all these should probably called attr1, attr2, attr3, ...
    osname = models.CharField(max_length=256, db_index=True)
    version = models.CharField(max_length=32, db_index=True)
    lang = models.CharField(max_length=32, db_index=True)
    country = models.CharField(max_length=2, db_index=True)

    count = models.IntegerField(default=1)

    def __unicode__(self):
        return u'%s / %s / %s / %s / %s' % (self.product, 
                                        self.osname,
                                        self.version,
                                        self.lang,
                                        self.country)

