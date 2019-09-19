#include <assert.h>
#include "common.h"
#include "point.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

void
point_translate(struct point *p, double x, double y)
{
	point_set(p, point_X(p) + x, point_Y(p) + y);
}

double
point_distance(const struct point *p1, const struct point *p2)
{
	return sqrt(pow(point_Y(p2) - point_Y(p1), 2) + pow(point_X(p2) - point_X(p1), 2));
}

int
point_compare(const struct point *p1, const struct point *p2)
{
	double p1EucLen = sqrt(pow(point_X(p1), 2) + pow(point_Y(p1), 2));
	double p2EucLen = sqrt(pow(point_X(p2), 2) + pow(point_Y(p2), 2));
	if (p1EucLen > p2EucLen){
		return 1;
	} else if (p1EucLen < p2EucLen){
		return -1;
	}
	return 0;
}
