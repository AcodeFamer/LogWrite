#include <pthread.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <aio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <sys/time.h>

//��־��������С���ɸ��ݴ������ܵ�����
#define LOG_BUFFER_SIZE  (1024*1024)

typedef struct  
{
	//linux aio
	struct aiocb log_aiocb;
	char* log_buff_1;
	char* log_buff_2;
	int log_used_size;
	int log_write_success_flag;
	pthread_mutex_t log_lock;
}log_struct;

/************************************************************
Function:  get_current_time
Description: ��ȡ��ǰʱ��
Input:   str_time  ����ǰ����ռ�
Output:    
***********************************************************/
void get_current_time(char *str_time)
{
    struct timeval tv;
	struct tm* cur_tm; 
	gettimeofday(&tv, NULL);
	cur_tm = localtime(&tv.tv_sec);
	int m_sec = tv.tv_usec / 10000;
	sprintf(str_time,"%d_%d_%d %d:%d:%d.%d  ",cur_tm->tm_year + 1900, cur_tm->tm_mon + 1, cur_tm->tm_mday, cur_tm->tm_hour, cur_tm->tm_min, cur_tm->tm_sec, m_sec);
} 


/************************************************************
Function:  log_write_callback
Description: �첽IO�ص�����
Input:   
Output:    
***********************************************************/
void log_write_callback(sigval_t sigval)
{
	log_struct *req;
	req = (log_struct*)sigval.sival_ptr;
	req->log_write_success_flag = 1;
	//printf("success\n");
}

/************************************************************
Function:  log_init
Description: ��ʼ����־��ȷ���ļ���δ���֣�
Input:   log_str:��־�ṹָ��    path:�ļ���(����·��)
Output:    0 �ɹ�    -1ʧ��
***********************************************************/
int log_init(log_struct *log_str, char *path)
{
	if(!log_str)
		return -1;
	bzero(log_str, sizeof(log_struct));
	if((log_str->log_aiocb.aio_fildes = open(path, O_RDWR | O_CREAT | O_EXCL)) < 0)
	{
		perror("open");
		return -1;
	}
    
        log_str->log_buff_1 = (char *)malloc(LOG_BUFFER_SIZE);
	if(!log_str->log_buff_1)
	{
		perror("malloc buffer 1");
		return -1;
	}

	log_str->log_buff_2 = (char *)malloc(LOG_BUFFER_SIZE);
	if(!log_str->log_buff_2)
	{
		perror("malloc buffer 2");
		return -1;
	}
	//log_str->log_aiocb.aio_nbytes = LOG_BUFFER_SIZE;
	log_str->log_aiocb.aio_offset = 0;
	log_str->log_aiocb.aio_buf = log_str->log_buff_1;
        log_str->log_used_size = 0;
	
	log_str->log_aiocb.aio_sigevent.sigev_notify = SIGEV_THREAD;
	log_str->log_aiocb.aio_sigevent.sigev_notify_function = log_write_callback;
	log_str->log_aiocb.aio_sigevent.sigev_notify_attributes = NULL;
	log_str->log_aiocb.aio_sigevent.sigev_value.sival_ptr = log_str;
	
    log_str->log_write_success_flag = 1;
	
    pthread_mutex_init(&log_str->log_lock, NULL);
    return 0;
}

/************************************************************
Function:  LOG_WRITE
Description: д��־
Input:   log_str:��־�ṹָ��    string:��־��Ϣ
Output:    0 �ɹ�    -1ʧ��
***********************************************************/
#define LOG_WRITE(log_str, string)     log_write(log_str, "File:%s Line: %d %s", __FILE__, __LINE__, string)
/************************************************************
Function:  log_write
Description: д��־
Input:   log_str:��־�ṹָ��    string:��־��Ϣ
Output:    0 �ɹ�    -1ʧ��
***********************************************************/
int log_write(log_struct *log_str, char *format, ...)
{
	struct timeval tv;
	struct tm* cur_tm; 
	char log_record[100];
	bzero(log_record, sizeof(log_record));


	get_current_time(log_record);
    //

    va_list arg_ptr;
	va_start(arg_ptr, format);
	int nWrittenBytes = vsprintf(log_record + strlen(log_record), format, arg_ptr);
	va_end(arg_ptr);	


	int len = strlen(log_record);
	int ret;
	if(len > LOG_BUFFER_SIZE)
	{
		printf("string is too long\n");
		return -1;
	}
	pthread_mutex_lock(&log_str->log_lock);
	//һ�������������û�������Ϣд����̣�����һ��������
	if(log_str->log_used_size + len + 1 > LOG_BUFFER_SIZE)
	{
		//�ȴ�IO�������
		while(!log_str->log_write_success_flag);
		//�ύ�첽IO����
		log_str->log_aiocb.aio_nbytes = log_str->log_used_size;
		log_str->log_write_success_flag = 0;
		ret = aio_write(&log_str->log_aiocb);
		if(ret != 0)
		{
			pthread_mutex_unlock(&log_str->log_lock);
			perror("aio_write");
			return -1;
		}
		//�ɹ��ύ����󣬸���offset
		log_str->log_aiocb.aio_offset += log_str->log_used_size;
		//һ��������������ʹ����һ����������ΪIO��������ʱ��
		if(log_str->log_aiocb.aio_buf == log_str->log_buff_1)
			log_str->log_aiocb.aio_buf = log_str->log_buff_2;
		else
			log_str->log_aiocb.aio_buf = log_str->log_buff_1;
		
		log_str->log_used_size = 0;
	}
	//������δ������־��Ϣ��ȡ������
	char *buffer = (char *)log_str->log_aiocb.aio_buf + log_str->log_used_size;
	strcpy(buffer, log_record);
	log_str->log_used_size += len;
	*(buffer + len) = '\n';
	++log_str->log_used_size;
	pthread_mutex_unlock(&log_str->log_lock);
	return 0;
}

/************************************************************
Function:  log_close
Description: �������ر���־������������δд�������д����̣�
Input:   
Output:    0 �ɹ�    -1ʧ��
***********************************************************/
int log_close(log_struct *log_str)
{
	int ret;
	log_str->log_aiocb.aio_nbytes = log_str->log_used_size;
	log_str->log_write_success_flag = 0;
	ret = aio_write(&log_str->log_aiocb);
	if(ret != 0)
	{
		pthread_mutex_unlock(&log_str->log_lock);
		perror("aio_read");
		return -1;
	}
	while(!log_str->log_write_success_flag);
	free(log_str->log_buff_1);
	free(log_str->log_buff_2);
	pthread_mutex_destroy(&log_str->log_lock);
	return 0;
}

int main()
{
	log_struct  log_s;
	log_init(&log_s, "1.txt");
	int i;
    for(i=0;i<10;++i)
    {
        sleep(1);
		LOG_WRITE(&log_s, "abcdefg");
		//log_s.log_aiocb.aio_offset += 8;
		//log_write(&log_s, "bbbbbb");
    }
	log_close(&log_s);
    sleep(1);
}
