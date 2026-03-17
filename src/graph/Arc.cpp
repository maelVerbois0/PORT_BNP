#include "Arc.h"

Arc::Arc(int id, ArcType ty, int o, int d, int w, int c, int t):ID(id),type(ty),from(o),to(d),cost(w * t + c)
{
}

Arc::Arc(int id, ArcType ty, int o, int d):ID(id),type(ty),from(o),to(d)
{
}


