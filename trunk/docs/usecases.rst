.. _usecases:

Use cases
=============================================================================


Generating Hashes
-------------------------------------------------------------------------

See :ref:`creating_hashes` for now


Torrent server with webseeds
-------------------------------------------------------------------------

See :ref:`configuring_torrent_generation` for now


...


Giving mirrors a different weight - balancing the load
-------------------------------------------------------------------------

The main criterion for load balancing is of course the network proximity, and
(failing that) the geographic location (country/region); but sometimes there
are several matching mirrors that are in the same country or network. Then
there are two additional mechanisms for choosing the best mirror:

1) influencing preference by an assigned "score" value
2) geographical distance

These two mechanisms work together.  Let's look at them in detail now.

1) Score value
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``score`` value that is assigned to each mirror is a unitless number
where the absolute value doesn't matter, but the relative height in
comparison to other mirrors makes the difference. The default value is always
``100``. If mirrors have the same score, they are randomly used equally
often.  Thus, the load is distributed equally. If the values differ, the
randomization becomes "weighted" towards the mirrors with higher scores.

If there is only one mirror in a country, the value doesn't matter, because
country is the stronger criterion. When there are several mirrors per
country, you'll often simply stick to the default value of 100. However, if
there's a small mirror that can't take much traffic, you can assign it a e.g.
20 or 50, and it will get much less load. How much, depends on the number of
number of other mirrors available in the same country. If, on the other hand,
you have a particular powerful mirror, you could assign it a score of 200, for
instance.

Here's an example with three mirrors in different combinations of score values::

  score value              100    100    100
  percentage of requests   33%    33%    33%

  score value              100     50     50
  percentage of requests   60%    20%    20%

  score value              100    200     10
  percentage of requests   25%    73%     2%


In real life, you might have more mirrors. The effect of score values can be
approximated with the following formula::

                   s
  P = ---------------------------- * 100
         s + (s2 + s3 + ... + sn)

  where
  P = percentage of requests to a mirror
  s = score of mirror
  s2...sn = scores of the other mirrors


Imagine that you have a mirror with ``score=50``, and other
mirrors in the same country with the following scores:
``150, 100, 100, 100, 100, 50, 50, 30``. Thus,

  50 / (50 + 150+100+100+100+100+50+50+30) * 100 = 6%

about 6% of requests will routed to that mirror.

(However, also remeber that mirrors might not always be complete mirrors, so
if they don't have certain files, they are automatically left out from the
equation. The calculation is always file-based, and thus never static.)




2) Geographical distance
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Geographical distance is calculated by an approximation that is both
lightweight and fast, but sufficiently accurate. Similar as the score value,
it's not about absolute numbers in a certain unit like km. It's more about
giving mirrors a relative weight according to the distance. 

Each mirror is rated with this formula (C code from the Apache module::

  new->dist = (int) ( sqrt( pow((lat - new->lat), 2) + pow((lng - new->lng), 2) ) * 1000 );

This approximation is simple enough but works around the (spherical) globe.

Mirrors are then ranked against each other according to the calculated ``dist``
values. Internally, this can all be done with simple&quick integer arithmetic.

Specifically, at this point we also give the **score** some influence::

  d = mirror->dist + distprio / mirror->score;

where::

  int distprio = DISTANCE_PRIO / arr->nelts;

where ``arr->nelts`` is the number of mirrors in that particular group in that
the ranking is calculated. ``DISTANCE_PRIO`` is defined to a number which you
could change at compile time, but where this default value gives about the
results I wanted::

  #define DISTANCE_PRIO 2000000

A comment in the code reads::

  /* the smaller, the smaller the effect of a raised prio in comparison to distance */
  /* 5000000 -> mirror in 200km distance is preferred already when it has prio 100
   * 1000000 -> mirror in 200km distance is preferred not before it has prio 300
   * (compared to 100 as normal priority for other mirrors, and tested in
   * Germany, which is a small country with many mirrors) */

You see, both the geographical distance and the score value work together and
both has some influence. This prevents the choice of a mirror that's either far
away or has a low score value, or one of them -- and vice versa.
