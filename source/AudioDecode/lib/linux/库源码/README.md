# 1.����alsa  
./configure --prefix=/root/Desktop/alsa --enable-static=yes --enable-shared=no  
make -j16  
make install  
  
  
# 2.����SDL  
./configure --prefix=/root/Desktop/SDL2 --enable-alsa --with-alsa-prefix=/root/Desktop/alsa/lib  --with-alsa-inc-prefix=/root/Desktop/alsa/include  
make -j16  
make install  

# 3.����ffmpeg  
����ffmpeg�ο���https://github.com/yundiantech/FFMPEG_DEMO/tree/master/ffmpeg-src

