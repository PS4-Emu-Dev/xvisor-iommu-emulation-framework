#/**
# Copyright (c) 2021 Himanshu Chauhan.
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
# @author Himanshu Chauhan (hchauhan@xvisor-x86.org)
# @brief list of board specific objects to be build
# */

board-common-objs-y+= devices/video/vga.o
board-common-objs-y+= devices/video/svga.o
board-common-objs-y+= devices/video/fb_console.o
board-common-objs-y+= devices/video/fb_early_console.o
