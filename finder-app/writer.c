 #include <syslog.h>
 #include <fcntl.h>
 #include <string.h>
 #include <errno.h>
 #include <unistd.h>

 int main(int argc, char *argv[])
{
openlog("writer", LOG_PID, LOG_USER);
if (argc != 3)
{
syslog(LOG_ERR, "Invalid number of arguments. Usage: %s <writefile> <writestr>", argv[0]);
closelog();
return 1;
}

const char *writefile = argv[1];
const char *writestr = argv[2];

syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);
int fd = open(writefile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
if (fd < 0)
{
syslog(LOG_ERR, "open() failed for %s: %s", writefile, strerror(errno));
closelog();
return 1;
}
size_t len = strlen(writestr);
ssize_t written = write(fd, writestr, len);
if(written < 0) 
{
syslog(LOG_ERR, "write() failed for %s: %s", writefile, strerror(errno));
close(fd);
closelog();
return 1;
}
if ((size_t) written != len)
{
syslog(LOG_ERR, "Partial write to %s: expected %zu, wrote %zd", writefile, len, written);
close(fd);
closelog();
return 1;

}
if (close(fd) < 0)

{
syslog(LOG_ERR, "close() failed for %s: %s", writefile, strerror(errno));
closelog();
return 1;
}

closelog();
return 0;


}
