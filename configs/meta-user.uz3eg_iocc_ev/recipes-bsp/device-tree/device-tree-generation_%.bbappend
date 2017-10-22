SRC_URI_append ="\ 
	file://system-user.dtsi \ 
	file://uz3eg_iocc_ev_clocks.dtsi \ 
	file://uz3eg_iocc_ev_flir_lepton.dtsi \ 
	file://uz3eg_iocc_ev_tdm114_cam1.dtsi \ 
	file://uz3eg_iocc_ev_tdm114_cam2.dtsi \ 
"                                     

FILESEXTRAPATHS_prepend := "${THISDIR}/files:"
