#include <stdio.h>			 // 입출력 사용 헤더파일
#include <stdlib.h>			 // exit() 사용 헤더파일
#include <string.h>			 // 문자열 사용 헤더파일
#include <unistd.h>			 // C 컴파일러 사용 헤더파일
#include <signal.h>			 // Signal 사용 헤더파일
#include <errno.h>			 // errno 사용 헤더파일
#include <softPwm.h>		 // pwm 사용 헤더파일
#include <wiringPi.h>		 // wiringPi 사용 헤더파일
#include <wiringPiSPI.h> // wiringPiSPI 사용
#include <stdint.h>
#include <sys/types.h>
#include <pthread.h> // 스레드 사용 헤더파일

#define CS_MCP3208 8			// BCM_GPIO 8, 조도 센서
#define LEDBAR 12					// BCM_GPIO 12, led
#define SPI_CHANNEL 0			// SPI 통신 채널
#define SPI_SPEED 1000000 // 1Mhz, SPI 통신 속도
#define RGBLEDPOWER 19		// BCM_GPIO 19, RGB
#define RED 2							// BCM_GPIO 2 - OUT
#define GREEN 3						// BCM_GPIO 3 - OUT
#define BLUE 4						// BCM_GPIO 4 - OUT
#define trigPin 18				// BCM_GPIO 18, 초음파 센서
#define echoPin 21				// BCM_GPIO 21, 초음파 센서
#define BUZCONTROL 20			// BCM_GPIO 20, Buzzer
#define FAN 6							// BCM_GPIO 6, Fan
#define DHTPIN 7					// BCM_GPIO 7, 온습도 센서

#define MAX_THREAD 4
#define MAXTIMINGS 85
#define DELAYTIME 1000

static int dht22_dat[5] = {0, 0, 0, 0, 0};

int read_mcp3208_adc(unsigned char adcChannel); // SPI통신을 통한 조도값 센싱 함수
void Bpluspinmodeset(void);											// pinMode 설정 함수
void setRGB(int r, int g, int b);								// rgb 설정 함수
void sig_handler(int signo);										// SIGINT 핸들러 함수

void *threadlight(void *data);	// 조도값 센싱을 통한 LEDBAR 제어 함수
void *threadrgb(void *data);		// web과 통신을 통한 RGB 제어 함수
void *threadbuzzer(void *data); // 초음파 센싱을 통한 부저 제어 함수
void *threaddht22(void *data);	// 온도 센싱을 통한 팬 제어 함수

// pinMode 설정 함수
void Bpluspinmodeset(void)
{
	// light
	pinMode(LEDBAR, OUTPUT);
	pinMode(CS_MCP3208, OUTPUT);

	// rgb
	pinMode(RGBLEDPOWER, OUTPUT);
	pinMode(RED, OUTPUT);
	pinMode(GREEN, OUTPUT);
	pinMode(BLUE, OUTPUT);

	// buzzer
	pinMode(trigPin, OUTPUT);
	pinMode(echoPin, INPUT);
	pinMode(BUZCONTROL, OUTPUT);

	// Fan
	pinMode(FAN, OUTPUT);
}

// SPI통신을 통한 조도값 센싱 함수
int read_mcp3208_adc(unsigned char adcChannel)
{
	unsigned char buff[3];
	int adcValue = 0;

	buff[0] = 0x06 | ((adcChannel & 0x07) >> 2);
	buff[1] = ((adcChannel & 0x07) << 6);
	buff[2] = 0x00;

	digitalWrite(CS_MCP3208, 0);
	wiringPiSPIDataRW(SPI_CHANNEL, buff, 3); // SPI_CHANNEL로부터 buff에 데이터 read

	buff[1] = 0x0f & buff[1];
	adcValue = (buff[1] << 8) | buff[2];

	digitalWrite(CS_MCP3208, 1);

	return adcValue;
}

// 조도값 센싱을 통한 LEDBAR 제어 함수
void *threadlight(void *data)
{
	unsigned char adcChannel_light = 0;
	int adcValue_light = 0; // 조도값
	char lightbuf[4096] = "";

	pthread_t tid = pthread_self();

	// 조도값을 1초마다 받아오며 밝으면 LEDBAR를 끄거나 약하게
	// 어두우면 LEDBAR를 점점 밝게 출력
	// led가 켜지면 현재의 조도값과 led 출력 퍼센트를 DB에 저장
	while (1)
	{
		adcValue_light = read_mcp3208_adc(adcChannel_light);
		int ledvalue = 0;

		printf("light sensor = %u\n", adcValue_light);
		if (adcValue_light >= 1500)
			ledvalue = 100;
		else if (adcValue_light >= 1400)
			ledvalue = 90;
		else if (adcValue_light >= 1300)
			ledvalue = 80;
		else if (adcValue_light >= 1200)
			ledvalue = 70;
		else if (adcValue_light >= 1100)
			ledvalue = 60;
		else if (adcValue_light >= 1000)
			ledvalue = 50;
		else if (adcValue_light >= 900)
			ledvalue = 40;
		else if (adcValue_light >= 800)
			ledvalue = 30;
		else if (adcValue_light >= 700)
			ledvalue = 20;
		else if (adcValue_light >= 600)
			ledvalue = 10;
		else if (adcValue_light >= 500)
			ledvalue = 0;

		if (ledvalue != 0)
		{
			sprintf(lightbuf, "curl '113.198.137.181:2001/DB/lightin.php?v=%u&l=%d'", adcValue_light, ledvalue);
			system(lightbuf);
		}

		softPwmWrite(LEDBAR, ledvalue);
		delay(DELAYTIME);
	}
}

// web과 통신을 통한 RGB 제어 함수
void *threadrgb(void *data)
{
	char buf[4096] = "";
	FILE *fp;

	// Web에서 선택한 색온도값이 저장된 DB에서 받아와 RGB값 설정
	while (1)
	{
		sprintf(buf, "curl -s '113.198.137.181:2001/DB/act_color.php'");

		if ((fp = popen(buf, "r")) == NULL)
		{
			return 1;
		}
		while (fgets(buf, 255, fp) != NULL)
		{
			printf("%s\n", buf);
		}

		softPwmCreate(RED, 0, 255);
		softPwmCreate(GREEN, 0, 255);
		softPwmCreate(BLUE, 0, 255);

		if (strcmp(buf, "red\n") == 0)
			setRGB(255, 162, 057);
		if (strcmp(buf, "blue\n") == 0)
			setRGB(200, 250, 255);
		if (strcmp(buf, "white\n") == 0)
			setRGB(255, 250, 220);
		if (strcmp(buf, "none\n") == 0)
			setRGB(0, 0, 0);

		delay(DELAYTIME);
		pclose(fp);
	}
}

// rgb 설정 함수
void setRGB(int r, int g, int b)
{
	softPwmWrite(RED, r);
	softPwmWrite(GREEN, g);
	softPwmWrite(BLUE, b);
}

// 초음파 센싱을 통한 부저 제어 함수
void *threadbuzzer(void *data)
{
	int distance = 0;
	int pulse = 0;
	char disbuf[4096] = "";

	long startTime;
	long travelTime;

	while (1)
	{
		digitalWrite(trigPin, LOW);
		usleep(2);
		digitalWrite(trigPin, HIGH);
		usleep(20);
		digitalWrite(trigPin, LOW);

		while (digitalRead(echoPin) == LOW)
			;
		startTime = micros();

		while (digitalRead(echoPin) == HIGH)
			;
		travelTime = micros() - startTime;

		distance = travelTime / 58;

		printf("Distance: %dcm\n", distance);

		// 거리가 10보다 작아지면 부저를 울리고 10보다 멀어지면 부저를 끈다.
		// 거리값과 부저의 on, off값 DB에 저장
		if (distance < 10)
		{
			sprintf(disbuf, "curl '113.198.137.181:2001/DB/distancein.php?v=%u&b=on'", distance);
			system(disbuf);
			digitalWrite(BUZCONTROL, 1);
		}
		else
		{
			sprintf(disbuf, "curl '113.198.137.181:2001/DB/distancein.php?v=%u&b=off'", distance);
			system(disbuf);
			digitalWrite(BUZCONTROL, 0);
		}
		delay(DELAYTIME);
	}
}

static uint8_t sizecvt(const int read)
{
	if (read > 255 || read < 0)
	{
		printf("Invalid data from wiringPi library\n");
		exit(EXIT_FAILURE);
	}

	return (uint8_t)read;
}

// 온도 센싱을 통한 팬 제어 함수
void *threaddht22(void *data)
{
	char dhtbuf[4096] = "";
	while (1)
	{
		uint8_t laststate = HIGH;
		uint8_t counter = 0;
		uint8_t j = 0, i;

		dht22_dat[0] = dht22_dat[1] = dht22_dat[2] = dht22_dat[3] = dht22_dat[4] = 0;

		pinMode(DHTPIN, OUTPUT);
		digitalWrite(DHTPIN, HIGH);
		delay(10);
		digitalWrite(DHTPIN, LOW);
		delay(18);

		digitalWrite(DHTPIN, HIGH);
		delayMicroseconds(40);

		pinMode(DHTPIN, INPUT);

		for (i = 0; i < MAXTIMINGS; i++)
		{
			counter = 0;
			while (sizecvt(digitalRead(DHTPIN)) == laststate)
			{
				counter++;
				delayMicroseconds(1);
				if (counter == 255)
				{
					break;
				}
			}
			laststate = sizecvt(digitalRead(DHTPIN));
			if (counter == 255)
				break;
			if ((i >= 4) && (i % 2 == 0))
			{
				dht22_dat[j / 8] <<= 1;
				if (counter > 16)
					dht22_dat[j / 8] |= 1;
				j++;
			}
		}
		// 데이터값이 좋을 때만 DB에 저장
		if ((j >= 40) && (dht22_dat[4] == ((dht22_dat[0] + dht22_dat[1] + dht22_dat[2] + dht22_dat[3]) & 0xFF)))
		{
			float t, h;
			h = (float)dht22_dat[0] * 256 + (float)dht22_dat[1];
			h /= 10;
			t = (float)(dht22_dat[2] & 0x7F) * 256 + (float)dht22_dat[3];
			t /= 10.0;
			if ((dht22_dat[2] & 0x80) != 0)
				t *= -1;
			if (t > 20.0)
				digitalWrite(FAN, 1);
			else if (t < 15.0)
				digitalWrite(FAN, 0);
			printf("Humidity = %.2f %% Temperature = %.2f *C \n", h, t);
			sprintf(dhtbuf, "curl '113.198.137.181:2001/DB/dhtin.php?h=%.2f&t=%.2f'", h, t);
			system(dhtbuf);
		}
		else
		{
			printf("Data not good, skip\n");
		}
		delay(DELAYTIME);
	}
}

// ctrl-c 로 종료시 실행되는 함수
void sig_handler(int signo)
{
	printf("process stop\n");

	// LEDBAR off
	digitalWrite(LEDBAR, 0);

	// RGB off
	digitalWrite(RED, 0);
	digitalWrite(GREEN, 0);
	digitalWrite(BLUE, 0);
	digitalWrite(RGBLEDPOWER, 0);

	// Buzzer off
	digitalWrite(BUZCONTROL, 0);

	// Fan off
	digitalWrite(FAN, 0);

	exit(0);
}

int main()
{
	// BCM_GPIO 기준으로 PIN 번호 설정
	if (wiringPiSetupGpio() == -1)
	{
		fprintf(stdout, "Unable to start wiringPi :%s\n", strerror(errno));
		return 1;
	}
	// SPI통신채널과 속도를 설정
	if (wiringPiSPISetup(SPI_CHANNEL, SPI_SPEED) == -1)
	{
		fprintf(stdout, "wiringPiSPISetup Failed :%s\n", strerror(errno));
		return 1;
	}

	pthread_t tid[MAX_THREAD];

	signal(SIGINT, (void *)sig_handler);

	Bpluspinmodeset();

	softPwmCreate(LEDBAR, 0, 100);

	pthread_create(&tid[0], NULL, threadlight, NULL);
	pthread_create(&tid[1], NULL, threadrgb, NULL);
	pthread_create(&tid[2], NULL, threadbuzzer, NULL);
	pthread_create(&tid[3], NULL, threaddht22, NULL);

	for (int i = 0; i < MAX_THREAD; i++)
	{
		pthread_join(tid[i], NULL);
	}

	return 0;
}
