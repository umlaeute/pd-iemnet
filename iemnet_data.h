/* *********************************************+
 * iemnet
 *     networking for Pd
 *
 *  (c) 2010 IOhannes m zm�lnig
 *           Institute of Electronic Music and Acoustics (IEM)
 *           University of Music and Dramatic Arts (KUG), Graz, Austria
 *
 *  data handling structures
 *   think of these as private, no need to worry about them outside the core lib
 */

/* ---------------------------------------------------------------------------- */

/* This program is free software; you can redistribute it and/or                */
/* modify it under the terms of the GNU General Public License                  */
/* as published by the Free Software Foundation; either version 2               */
/* of the License, or (at your option) any later version.                       */
/*                                                                              */
/* This program is distributed in the hope that it will be useful,              */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of               */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                */
/* GNU General Public License for more details.                                 */
/*                                                                              */
/* You should have received a copy of the GNU General Public License            */
/* along with this program; if not, write to the Free Software                  */
/* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.  */
/*                                                                              */

/* ---------------------------------------------------------------------------- */


typedef struct _iemnet_floatlist {
  t_atom*argv;
  size_t argc;

  size_t size; // real size (might be bigger than argc)
} t_iemnet_floatlist;



#define t_iemnet_queue struct _iemnet_queue
EXTERN_STRUCT _iemnet_queue;

