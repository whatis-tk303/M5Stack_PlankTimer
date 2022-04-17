#include <M5Stack.h>
#include <M5StackUpdater.h>

#define LGFX_AUTODETECT // 自動認識 (M5Stack, M5StickC/CPlus, ODROID-GO, TTGO T-Watch, TTGO T-Wristband, LoLin D32 Pro, ESP-WROVER-KIT)
#include <LovyanGFX.hpp>

static LGFX lcd;

/* アプリ動作状態 */
#define STAT_UNKNOWN	(0)
#define STAT_IDLE       (1)
#define STAT_MEASURING  (2)
#define STAT_STOPPED    (3)

/* 状態遷移トリガー（ACT_INIT） */
#define ACT_INIT		(0)
#define ACT_RUNNING		(1)


#define COLOR_NORMAL		(0x00FF88U)		/* 通常の文字色               */
#define COLOR_STOP			(0xFF0000U) 	/* タイマー停止時の文字色      */
#define COLOR_BACK_OFF		(0x000000U)		/* 通常の背景色               */
#define COLOR_BACK_CLOCK	(0x002244U)		/* タイマーの背景色            */
#define COLOR_BACK_SELECT	(0x664422U)		/* 選択されている設定値の背景色 */
unsigned long g_color_clock;


/****************************************************************************
 * @brief 		時間計測クラス
 */
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


/****************************************************************************
 * @brief 		計測時間を描画する
 * @param [in]	time_count - 計測時間
 * @param [in]	light      - 点滅状態（true: 点灯）
 */
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


/****************************************************************************
 * @brief 		バッテリー状態を描画する
 */
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


/****************************************************************************
 * @brief 		設定時間の選択肢を描画する
 */
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

/****************************************************************************
 * @brief 		アプリ動作状態処理：Idle
 * @param [in]	action - この状態内で実施する実行指示：ACT_XXX
 */
void procstat_Idle(int action)
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


/****************************************************************************
 * @brief 		アプリ動作状態処理：Measuring
 * @param [in]	action - この状態内で実施する実行指示：ACT_XXX
 */
void procstat_Measuring(int action)
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


/****************************************************************************
 * @brief 		アプリ動作状態処理：Stop
 * @param [in]	action - この状態内で実施する実行指示：ACT_XXX
 */
void procstat_Stopped(int action)
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


/****************************************************************************
 * @brief		現在のアプリ状態の動作を実行する
 * @param [in]	state  - 実行するアプリ動作状態：STAT_XXX
 * @param [in]	action - ACT_INIT:    状態遷移直後の初期化処理を実行する
 *						 ACT_RUNNING: 定常時の状態の処理を実行する
 * @note
 *   - 主に画面描画の処理を実行する。その他、時間の管理、音の出力等。
 *   - 他の状態から遷移してきた最初は ACT_INIT が渡される
 *   - 描画は lcd.startWrite() ～ lcd.endWrite() の間で実施することで高速化する。
 *     （SPIによるLCDへの画像データ転送をまとめて行うので省電力にもなる（はず））
 *     - この間にいる時は他の SPIデバイスは使えない。
 *       （→ それでも他の SPIデバイスを使いたい場合は LCD.beginTransaction()を利用する）
 */
void proc_state(int state, int action)
{
	lcd.startWrite();

	switch(state)
	{
	case STAT_IDLE:
		procstat_Idle(action);
		break;

	case STAT_MEASURING:
		procstat_Measuring(action);
		break;

	case STAT_STOPPED:
		procstat_Stopped(action);
		break;
	}

	/* バッテリーの状態（充電中、バッテリーレベル） */
	draw_power_status();

	/* タイマー時間の選択肢 */
	draw_timer_select();

	lcd.endWrite();
}


/****************************************************************************
 * @brief 		状態を更新する
 * @param [in]	state - 現在の状態
 * @return 		更新後の状態
 */
int change_state(int state)
{
	int state_new = state;

	switch(state)
	{
	case STAT_IDLE:
		if (M5.BtnB.wasPressed())
		{
			state_new = STAT_MEASURING;
		}
		break;

	case STAT_MEASURING:
		if (M5.BtnB.wasPressed())
		{
			state_new = STAT_STOPPED;
		}
		else if (g_time_count.expired())
		{
			state_new = STAT_STOPPED;
		}
		break;
		
	case STAT_STOPPED:
		if (M5.BtnB.wasPressed())
		{
			state_new = STAT_IDLE;
		}
		break;
		
	default:
		/* （ここには来ない） */
		break;
	}

	return state_new;
}


/****************************************************************************
 * @brief 		Arduino関数：初期化
 */
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

	M5.Speaker.begin();
	M5.Power.begin();
}


/****************************************************************************
 * @brief 		Arduino関数：定常動作時の処理
 */
void loop() {
	static int state = STAT_IDLE;
	static int prev_state = STAT_UNKNOWN;
	static int action = ACT_INIT;

	/* ボタン入力の状態を更新する */
	M5.update();

	/* アプリ動作状態を更新する */
	state = change_state(state);

	/* アプリ動作状態に変化があったら、新しい状態の初期化処理を走らせる */
	action = (prev_state != state) ? ACT_INIT : ACT_RUNNING;
	prev_state = state;

	/* 現在のアプリ動作状態を実行する */
	proc_state(state, action);
}
