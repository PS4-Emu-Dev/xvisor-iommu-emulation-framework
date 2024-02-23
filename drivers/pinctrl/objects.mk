#/**
# Copyright (c) 2014 Pranav Sawargaonkar.
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
# @author Pranav Sawargaonkar (pranav.sawargaonkar@gmail.com)
# @brief list of driver objects
# */

drivers-objs-$(CONFIG_PINCTRL)		+= pinctrl/core.o
drivers-objs-$(CONFIG_PINCTRL)		+= pinctrl/pinctrl-utils.o
drivers-objs-$(CONFIG_PINCTRL)		+= pinctrl/devicetree.o
drivers-objs-$(CONFIG_PINMUX)		+= pinctrl/pinmux.o
drivers-objs-$(CONFIG_PINCONF)		+= pinctrl/pinconf.o
drivers-objs-$(CONFIG_GENERIC_PINCONF)	+= pinctrl/pinconf-generic.o
drivers-objs-$(CONFIG_PINCTRL_SUNXI)	+= pinctrl/pinctrl-sunxi.o
drivers-objs-$(CONFIG_PINCTRL_ROCKCHIP)	+= pinctrl/pinctrl-rockchip.o
