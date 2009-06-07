/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#include <nucleos/types.h>

/**
 * @brief Make a 64 bit number from two 32 bit halves
 */
uint64_t make64(unsigned long lo, unsigned long hi)
{
  uint64_t tmp_hi = hi;

  return (uint64_t)((tmp_hi<<32)|lo);
}