# Test UVC camera
#
# -m : capture mode 0-YUYV, 1-MJPEG
# -s : store mode 	0-YUYV, 1-MJPEG, 2-NOTHING
# -o : filename
# -c : count


# Test mt9m114
#v4l2-capture -w 1280 -h 720 -m 0 -s 1 -d /dev/video0 -o t -c 20
v4l2-capture -w 1280 -h 720 -m 1 -s 1 -d /dev/video0 -o t -c 1


