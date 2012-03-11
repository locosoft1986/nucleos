/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
struct mantissa {
	unsigned long h_32;
	unsigned long l_32;
};

struct EXTEND {
	short	sign;
	short	exp;
	struct mantissa mantissa;
#define m1 mantissa.h_32
#define m2 mantissa.l_32
};
	
