#include <M5Stack.h>
#include <M5StackUpdater.h>

#define LGFX_AUTODETECT // 自動認識 (M5Stack, M5StickC/CPlus, ODROID-GO, TTGO T-Watch, TTGO T-Wristband, LoLin D32 Pro, ESP-WROVER-KIT)
#include <LovyanGFX.hpp>

static LGFX lcd;


#define STAT_IDLE       (0)
#define STAT_MEASURING  (1)
#define STAT_STOPPED    (2)

#define ACT_INIT		(0)
#define ACT_RUNNING		(1)


int g_state;

#define COLOR_NORMAL		(0x00FF88U)		/* 通常の文字色               */
#define COLOR_STOP			(0xFF0000U) 	/* タイマー停止時の文字色      */
#define COLOR_BACK_OFF		(0x000000U)		/* 通常の背景色               */
#define COLOR_BACK_CLOCK	(0x002244U)		/* タイマーの背景色            */
#define COLOR_BACK_SELECT	(0x664422U)		/* 選択されている設定値の背景色 */
unsigned long g_color_clock;

class TimeCount {
public:
  /* constructor*/
  TimeCount()
  {
	  reset();
  }

	/**/
	void reset()
	{
		min = 0;
		sec = 0;
		count_sec = 0;
		alarm_enable = false;
		alarm_sec = 0;
	}

  /**/
  void countUp()
  {
	count_sec += 1;
	min = count_sec / 60;
	sec = count_sec % 60;

	/* min は99(分)を上限にする。その時の秒は 0秒で固定する */
	if (99 <= min)
	{
		min = 99;
		sec = 0;
	}
  }

	/**/
	void setAlarm(uint16_t set_sec)
	{
		alarm_enable = true;
		alarm_sec = set_sec;
	}

	/**/
	bool expired()
	{
		return ((alarm_enable) && (alarm_sec <= count_sec));
	}

public:
	int min;
	int sec;
  	uint16_t count_sec;	/* トータルカウント秒数 */
	bool alarm_enable;
	uint16_t alarm_sec;
};


TimeCount	g_time_count;

/****************************************************************************/
void setup() {
	/* LovyanLauncher対応：起動時にボタンAが押されていたらランチャーに戻る */
	if (digitalRead(BUTTON_A_PIN) == 0)
	{
	updateFromFS(SD);   //SDカードの "menu.bin" を読み込み
	ESP.restart();      // 再起動
	}

	lcd.init();
	lcd.setRotation(1);
	lcd.setBrightness(128);
	lcd.setColorDepth(16);  // RGB565の16ビットに設定

	Serial.begin(115200);

	g_state = STAT_IDLE;

	M5.Speaker.begin();
	M5.Power.begin();
}


/****************************************************************************/
void drawTime(const TimeCount &time_count, bool light = true) {
	lcd.setCursor(10, 40);
	lcd.setFont(&fonts::Font7);
	lcd.setTextSize(2.2);

	if (light)
	{
		lcd.setTextColor(g_color_clock, COLOR_BACK_CLOCK);
		lcd.printf("%02d:%02d", time_count.min, time_count.sec);
	}
	else
	{
		lcd.setTextColor(g_color_clock, COLOR_BACK_CLOCK);
		lcd.printf("%02d", time_count.min);

		lcd.setTextColor(COLOR_BACK_OFF, COLOR_BACK_CLOCK);
		lcd.printf(":");

		lcd.setTextColor(g_color_clock, COLOR_BACK_CLOCK);
		lcd.printf("%02d", time_count.sec);
	}
}


/****************************************************************************/
void draw_power_status()
{
	lcd.setCursor(2, 2);
	lcd.setFont(&fonts::FreeSansBold9pt7b);
	lcd.setTextSize(1.0);
	lcd.setTextColor(COLOR_NORMAL, COLOR_BACK_OFF);

	if (M5.Power.canControl())
	{
		const char *str_charge = "???";
		int8_t bat_level = -1;

		if (M5.Power.isCharging())
		{
			str_charge = "Charging";
		}
		else
		{
			str_charge = M5.Power.isChargeFull() ? "BattFULL" : "Battery";
		}

		bat_level = M5.Power.getBatteryLevel();

		lcd.printf("%8s:%03d%%", str_charge, bat_level);
	}
	else
	{
		lcd.printf("%8s:---%%", "Battery");
	}
}

/****************************************************************************/
void draw_timer_select()
{
	static struct {
		struct {
			int x, y;
		} pos;

		struct {
			int min, sec;
		} time;
	} selector[] = {
		{{30,  170}, {  2,  0}},
		{{130, 170}, {  3,  0}},
		{{230, 170}, {  5,  0}},
		{{ 80, 210}, { -1, -1}},	/* min == -1 indicates 'CUSTOM' */
	};
	const int NUM_SELECTOR = (sizeof(selector)/sizeof(selector[0]));


	for (int i = 0; i < NUM_SELECTOR; i++)
	{
		int x = selector[i].pos.x;
		int y = selector[i].pos.y;
		int min = selector[i].time.min;
		int sec = selector[i].time.sec;
		const char *str = "";

		lcd.setCursor(x, y);
		lcd.setTextColor(COLOR_NORMAL, COLOR_BACK_SELECT);

		if (min < 0)
		{
			min = 12;	/* TODO: shoud set to custom min. */
			sec = 34;	/* TODO: shoud set to custom sec. */
			str = "CUSTOM";
			lcd.setFont(&fonts::FreeSansBold9pt7b);
			lcd.setTextSize(1.4);
			lcd.printf("CUSTOM");
		}

		lcd.setFont(&fonts::Font7);
		lcd.setTextSize(0.5);
		lcd.printf(" %02d:%02d ", min, sec);
	}
}

/****************************************************************************/
void stateproc_Idle(int action)
{
	static bool flag_blink;
	static unsigned long prev_msec;
	unsigned long msec;

	if (action == ACT_INIT)
	{
		g_time_count.reset();
		prev_msec = millis();
		flag_blink = true;
	}

	msec = millis();
	g_time_count.reset();
	if (300 <= (msec - prev_msec))
	{
		prev_msec = msec;
		flag_blink = flag_blink ? false : true;
	}

	g_color_clock = flag_blink ? COLOR_NORMAL : COLOR_BACK_OFF;
drawTime(g_time_count);
}


/****************************************************************************/
void stateproc_Measuring(int action)
{
	static bool flag_blink;
	static unsigned long prev_msec;
	unsigned long msec;

	if (action == ACT_INIT)
	{
		g_color_clock = COLOR_NORMAL;
		prev_msec = millis();
		flag_blink = true;
		g_time_count.setAlarm(120);	/* TODO: for debug: 暫定的にアラームを固定で2分に設定してる */
	}

	msec = millis();
	unsigned long diff_time = msec - prev_msec;
	flag_blink = (diff_time < 500) ? true : false;
	if (1000 <= diff_time)
	{
		prev_msec = msec;
		g_time_count.countUp();
	}

	drawTime(g_time_count, flag_blink);
}


/****************************************************************************/
void stateproc_Stopped(int action)
{
	static bool flag_blink;
	static unsigned long prev_msec;
	static int prev_step;
	unsigned long msec;

	if (action == ACT_INIT)
	{
		g_color_clock = COLOR_STOP;
		prev_msec = millis();
		flag_blink = true;
		prev_step = -1;
	}

	msec = millis();
	unsigned long past = msec - prev_msec;
	if (500 <= past)
	{
		prev_msec = msec;
		flag_blink = flag_blink ? false : true;
	}

	g_color_clock = flag_blink ? COLOR_STOP : COLOR_NORMAL;
	drawTime(g_time_count);

	int step = past / 150;
	if ((step < 2) & (step != prev_step))
	{
		prev_step = step;
		uint16_t frequency = 880;
		uint32_t duration = 50;
		M5.Speaker.tone(frequency, duration);
	}
}


/****************************************************************************/
void loop() {
	static int prev_state = STAT_IDLE;
	static int action = ACT_INIT;

	prev_state = g_state;

	M5.update();

	switch(g_state)
	{
	case STAT_IDLE:
		if (M5.BtnB.wasPressed())
		{
			g_state = STAT_MEASURING;
		}
		break;

	case STAT_MEASURING:
		if (M5.BtnB.wasPressed())
		{
			g_state = STAT_STOPPED;
		}
		else if (g_time_count.expired())
		{
			g_state = STAT_STOPPED;
		}
		break;
		
	case STAT_STOPPED:
		if (M5.BtnB.wasPressed())
		{
			g_state = STAT_IDLE;
		}
		break;
		
	default:
		break;
	}


	if (prev_state != g_state)
	{
		action = ACT_INIT;
	}

	switch(g_state)
	{
	case STAT_IDLE:
		stateproc_Idle(action);
		break;

	case STAT_MEASURING:
		stateproc_Measuring(action);
		break;

	case STAT_STOPPED:
		stateproc_Stopped(action);
		break;
	}

	action = ACT_RUNNING;

	draw_power_status();
	draw_timer_select();
}
