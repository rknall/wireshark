/* packet-sdlc.c
 * Routines for SDLC frame disassembly
 *
 * $Id: packet-sdlc.c,v 1.1 2003/01/03 22:31:24 guy Exp $
 *
 * Ethereal - Network traffic analyzer
 * By Gerald Combs <gerald@ethereal.com>
 * Copyright 1998
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <glib.h>
#include <string.h>
#include <epan/packet.h>
#include "xdlc.h"

/*
 * See
 *
 *	http://www.protocols.com/pbook/sna.htm
 */

static int proto_sdlc = -1;
static int hf_sdlc_address = -1;
static int hf_sdlc_control = -1;

static gint ett_sdlc = -1;
static gint ett_sdlc_control = -1;

static dissector_handle_t sna_handle;
static dissector_handle_t data_handle;

static void
dissect_sdlc(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
	proto_tree	*sdlc_tree;
	proto_item	*sdlc_ti;
	guint8		address;
	guint16		control;
	int		sdlc_header_len;
	gboolean	is_response;
	tvbuff_t	*next_tvb;

	if (check_col(pinfo->cinfo, COL_PROTOCOL))
		col_set_str(pinfo->cinfo, COL_PROTOCOL, "SDLC");
	if (check_col(pinfo->cinfo, COL_INFO))
		col_clear(pinfo->cinfo, COL_INFO);

	address = tvb_get_guint8(tvb, 0);
	sdlc_header_len = 1;	/* address */

	/*
	 * XXX - is there something in the SDLC header that indicates
	 * how to interpret "command vs. response" based on the
	 * direction?
	 */
	if (pinfo->p2p_dir == P2P_DIR_SENT) {
		is_response = FALSE;
		if (check_col(pinfo->cinfo, COL_RES_DL_DST))
			col_set_str(pinfo->cinfo, COL_RES_DL_DST, "DCE");
		if (check_col(pinfo->cinfo, COL_RES_DL_SRC))
			col_set_str(pinfo->cinfo, COL_RES_DL_SRC, "DTE");
	}
	else {
		/* XXX - what if the direction is unknown? */
		is_response = TRUE;
		if (check_col(pinfo->cinfo, COL_RES_DL_DST))
		    col_set_str(pinfo->cinfo, COL_RES_DL_DST, "DTE");
		if (check_col(pinfo->cinfo, COL_RES_DL_SRC))
		    col_set_str(pinfo->cinfo, COL_RES_DL_SRC, "DCE");
	}

	if (tree) {
		sdlc_ti = proto_tree_add_item(tree, proto_sdlc, tvb, 0, -1,
		    FALSE);
		sdlc_tree = proto_item_add_subtree(sdlc_ti, ett_sdlc);

		proto_tree_add_uint(sdlc_tree, hf_sdlc_address, tvb, 0, 1,
		    address);
	} else {
		sdlc_ti = NULL;
		sdlc_tree = NULL;
	}

	/*
	 * XXX - SDLC has a mod-128 mode as well as a mod-7 mode.
	 * We can infer the mode from an SNRM/SRME frame, but if
	 * we don't see one of them, we may have to have a preference
	 * to control what to use.
	 */
	control = dissect_xdlc_control(tvb, 1, pinfo, sdlc_tree, hf_sdlc_control,
	    ett_sdlc_control, is_response, FALSE);
	sdlc_header_len += XDLC_CONTROL_LEN(control, FALSE);

	if (tree)
		proto_item_set_len(sdlc_ti, sdlc_header_len);

	/*
	 * XXX - is there an FCS at the end, at least in Sniffer
	 * captures?  (There doesn't appear to be.)
	 */
	next_tvb = tvb_new_subset(tvb, sdlc_header_len, -1, -1);
	if (XDLC_IS_INFORMATION(control)) {
		/* call the SNA dissector */
		call_dissector(sna_handle, next_tvb, pinfo, tree);
	} else
		call_dissector(data_handle, next_tvb, pinfo, tree);
}

void
proto_register_sdlc(void)
{
	static hf_register_info hf[] = {
		{ &hf_sdlc_address,
		  { "Address Field", "sdlc.address", FT_UINT8, BASE_HEX,
		     NULL, 0x0, "Address", HFILL }},

		{ &hf_sdlc_control,
		  { "Control Field", "sdlc.control", FT_UINT16, BASE_HEX,
		    NULL, 0x0, "Control field", HFILL }},
	};
	static gint *ett[] = {
		&ett_sdlc,
		&ett_sdlc_control,
	};

	proto_sdlc = proto_register_protocol(
	    "Synchronous Data Link Control (SDLC)", "SDLC", "sdlc");
	proto_register_field_array(proto_sdlc, hf, array_length(hf));
	proto_register_subtree_array(ett, array_length(ett));
}

void
proto_reg_handoff_sdlc(void)
{
	dissector_handle_t sdlc_handle;

	/*
	 * Get handle for the SNA dissector.
	 */
	sna_handle = find_dissector("sna");
	data_handle = find_dissector("data");

	sdlc_handle = create_dissector_handle(dissect_sdlc, proto_sdlc);
	dissector_add("wtap_encap", WTAP_ENCAP_SDLC, sdlc_handle);
}
