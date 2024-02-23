#/**
# Copyright (c) 2013 Sukanto Ghosh.
# All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
# @file objects.mk
# @author Sukanto Ghosh (sukantoghosh@gmail.com)
# @brief list of Generic board objects.
# */

board-objs-$(CONFIG_GENERIC_VEXPRESS)+= vexpress.o
board-objs-$(CONFIG_GENERIC_BCM2836)+= bcm2836.o
board-objs-$(CONFIG_GENERIC_FOUNDATION_V8)+= foundation-v8.o
board-objs-$(CONFIG_GENERIC_RK3399)+= rk3399.o
