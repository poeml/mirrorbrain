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

The ``score`` value that is assigned to each mirrir is a unitless number
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

  score    100 100 100
  chance   33% 33% 33%

  score    100  50  50
  chance   60% 20% 20%

  score    100 200  10
  chance   25% 73%  2%


In real life, you might have more mirrors. The effect of score values can be
approximated with the following formula::

                   s1
  P1 = ---------------------------- * 100
         s1 + (s2 + s3 + s4 + ...)

  where
  P1 = percentage of requests to a mirror
  s1 = score of mirror
  S2 ... = scores of the other mirrors


Imagine that you have a mirror with ``score=50``, and other
mirrors in the same country with the following scores:
``150, 100, 100, 100, 100, 50, 50, 30``. 



  50 / (50 + 150+100+100+100+100+50+50+30) = 0.06


or, more general::
  


                    50 
    ----------------------------------- * 100 = 6%
     50 + 150+100+100+100+100+50+50+30

Thus, about 6% of requests will routed to that mirror.

(However, also remeber that mirrors might not always be complete mirrors, so
if they don't have certain files, they are automatically left out from the
equation. The calculation is always file-based, and thus never static.)






.. /* the smaller, the smaller the effect of a raised prio in comparison to distance */
.. /* 5000000 -> mirror in 200km distance is preferred already when it has prio 100
..  * 1000000 -> mirror in 200km distance is preferred not before it has prio 300
..  * (compared to 100 as normal priority for other mirrors, and tested in
..  * Germany, which is a small country with many mirrors) */
.. #define DISTANCE_PRIO 2000000


