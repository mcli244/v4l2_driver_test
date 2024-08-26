#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <linux/videodev2.h>

int main(int argc, char **argv)
{
        int fd;
        int ret = 0;
        struct v4l2_capability cap;

        if (argc != 2)
        {
                printf("Usage: v4l2_test <device>\n");
                printf("example: v4l2_test /dev/video0\n");
                return -1;
        }

        fd = open(argv[1], O_RDWR);
        if (fd < 0)
        {
                printf("open video device fail\n");
                return -1;
        }

        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0)
        {
                printf("Get video capability error!\n");
                return -1;
        }

        printf("device_caps:0x%x.\n", cap.device_caps);
        if (!(cap.device_caps & V4L2_CAP_VIDEO_CAPTURE))
        {
                printf("Video device not support capture!\n");
                close(fd);
                return -1;
        }

        printf("Support capture!\n");

        struct v4l2_input inp;
        for (int i=0; i<4; i++)
        {
                inp.index = i;
                if(0 == ioctl(fd, VIDIOC_ENUMINPUT, &inp))
                {
                        printf("%s : std: 0x%08llx\n", inp.name, inp.std);
                }
        }

        
        struct v4l2_fmtdesc fmtdesc;
        
        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmtdesc.index = 0;
        for (int i=0; i<4; i++)
        {
                fmtdesc.index = i;
                if(0 == ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc))
                {
                        printf("%d %s - 0x%x \n", i, fmtdesc.description, fmtdesc.pixelformat);
                }
        }

        struct v4l2_format g_v4l2_fmt;
        memset(&g_v4l2_fmt, 0, sizeof(g_v4l2_fmt));
        
        g_v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ret = ioctl(fd, VIDIOC_G_FMT, &g_v4l2_fmt);
        if(ret != 0)
        {       
                printf("ret:%d\n", ret);
        }

        printf("width:%d height:%d pixelformat:0x%x type:0x%x\n", g_v4l2_fmt.fmt.pix.width, g_v4l2_fmt.fmt.pix.height, g_v4l2_fmt.fmt.pix.pixelformat, g_v4l2_fmt.type);
        
        struct v4l2_format v4l2_fmt;
        v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
        v4l2_fmt.fmt.pix.width = 480; //宽度
        v4l2_fmt.fmt.pix.height = 320; //高度
        v4l2_fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV; //像素格式
        v4l2_fmt.fmt.pix.field = V4L2_FIELD_ANY; 
        if(0 == ioctl(fd, VIDIOC_S_FMT, &v4l2_fmt))
        {
                printf("set succ\n");
        }

        memset(&g_v4l2_fmt, 0, sizeof(g_v4l2_fmt));
        g_v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // 这个是v4l2内部做处理的
        ret = ioctl(fd, VIDIOC_G_FMT, &g_v4l2_fmt);
        if(ret != 0)
        {       
                printf("ret:%d\n", ret);
        }

        printf("width:%d height:%d pixelformat:0x%x type:0x%x\n", g_v4l2_fmt.fmt.pix.width, g_v4l2_fmt.fmt.pix.height, g_v4l2_fmt.fmt.pix.pixelformat, g_v4l2_fmt.type);
        

        struct v4l2_requestbuffers req;
        req.count = 2; //缓存数量
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        ret = ioctl(fd, VIDIOC_REQBUFS, &req);
        if (ret < 0)
        {
                printf("VIDIOC_REQBUFS failed ret:%d\n", ret);
        }

        sleep(1);



        close(fd);

        return 0;
}