SRC_URI += "file://user_uz3eg_iocc_ev.cfg"

SRC_URI_append = " \                  
	file://idt5901-2017.1-v1.0.patch \ 
	file://tdnext-2017.2-v1.0.patch \ 
"                                     

FILESEXTRAPATHS_prepend := "${THISDIR}/${PN}:"
