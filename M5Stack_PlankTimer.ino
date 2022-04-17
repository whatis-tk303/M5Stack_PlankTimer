#include <M5Stack.h>
#include <M5StackUpdater.h>

#define LGFX_AUTODETECT // 自動認識 (M5Stack, M5StickC/CPlus, ODROID-GO, TTGO T-Watch, TTGO T-Wristband, LoLin D32 Pro, ESP-WROVER-KIT)
#include <LovyanGFX.hpp>

static LGFX lcd;

/* アプリ動作状態 */
#define STAT_UNKNOWN	(0)		/* ダミー状態（状態の実体は無い） */
#define STAT_IDLE       (1)
#define STAT_MEASURING  (2)
#define STAT_STOPPED    (3)

/* イベント */
#define EVT_NONE			(0)
#define EVT_INIT			(1)
#define EVT_BTN_A_PRESSED	(2)
#define EVT_BTN_B_PRESSED	(3)
#define EVT_BTN_C_PRESSED	(4)
#define EVT_TIME_EXPIRED	(5)


#define COLOR_NORMAL			(0x00FF88U)		/* 通常の文字色                 */
#define COLOR_STOP				(0xFF0000U) 	/* タイマー停止時の文字色        */
#define COLOR_BACK_OFF			(0x000000U)		/* 通常の背景色                 */
#define COLOR_BACK_CLOCK		(0x002244U)		/* タイマーの背景色              */
#define COLOR_BACK_SELECTED		(0x999900U)		/* 選択されている設定値の背景色   */
#define COLOR_BACK_NOT_SELECT	(0x553311U)		/* 選択されていない設定値の背景色 */
unsigned long g_color_clock;


/****************************************************************************
 * @brief 		設定時間を選択するクラス
 */
class TimeSelector {
public:
	typedef enum {
		PRESET_TIME_2_MIN,
		PRESET_TIME_3_MIN,
		PRESET_TIME_5_MIN,
		PRESET_TIME_CUSTOM,
	} PresetTime;

	/* constructor */
	TimeSelector()
	{
		preset_time_ = PRESET_TIME_2_MIN;
	}

	/**/
	void set_preset(PresetTime pt)
	{
		preset_time_ = pt;
	}

	/**/
	PresetTime get_preset()
	{
		return preset_time_;
	}

	/*  */
	int get_sec()
	{
		return get_preset_sec(preset_time_);
	}

	/* (class function) 指定された PresetTime の時間（秒）を取得する */
	static int get_preset_sec(PresetTime pt)
	{
		int sec = 0;
		switch(pt)
		{
		case PRESET_TIME_2_MIN:		sec = 2 * 60;	break;
		case PRESET_TIME_3_MIN:		sec = 3 * 60;	break;
		case PRESET_TIME_5_MIN:		sec = 5 * 60;	break;
		case PRESET_TIME_CUSTOM:	sec = 0 * 60;	break;
		}

		return sec;
	}

private:
	PresetTime preset_time_;

};


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

	/* 時間をリセットする */
	void reset()
	{
		count_sec = 0;
	}

	/* カウント時間を1秒進める */
	void countUp()
	{

		/* min は99(分)を上限にする。その時の秒は 0秒で固定する */
		if (count_sec < (99 * 60))
		{
			count_sec += 1;
		}
	}

	/* 時間をセットする（秒単位） */
	void set_time(unsigned int sec)
	{
		count_sec = sec;
	}

	/* 時間を取得する（秒単位） */
	int get_time() const
	{
		return count_sec;
	}

	/* 現在のカウント時間の分の部分を取得する */
	int get_min() const
	{
		return (count_sec / 60);
	}

	/* 現在のカウント時間の秒の部分を取得する */
	int get_sec() const
	{
		return (count_sec % 60);
	}

private:
  	uint16_t count_sec;	/* トータルカウント秒数 */
};


/****************************************************************************
 * @brief 		時間計測の管理クラス
 */
class AlarmManager {
public:
	/* constructor */
	AlarmManager(TimeCount &tc) : time_count_(tc)
	{
		reset();
	}

	/**/
	void reset()
	{
		alarm_enable = false;
	}

	/**/
	void set_alarm(uint16_t set_sec)
	{
		alarm_enable = true;
		alarm_time_.set_time(set_sec);
	}

	/**/
	bool is_expired()
	{
		int current_sec = time_count_.get_time();
		int target_sec = alarm_time_.get_time();
		return ((alarm_enable) && (target_sec <= current_sec));
	}

private:
	TimeCount alarm_time_;		/* アラーム時間の設定 */
	TimeCount &time_count_;		/* 計測中の時間（参照） */
	bool alarm_enable;
};


TimeCount	g_time_count;
AlarmManager g_alarm_manager(g_time_count);
TimeSelector g_time_selector;


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
		lcd.printf("%02d:%02d", time_count.get_min(), time_count.get_sec());
	}
	else
	{
		lcd.setTextColor(g_color_clock, COLOR_BACK_CLOCK);
		lcd.printf("%02d", time_count.get_min());

		lcd.setTextColor(COLOR_BACK_OFF, COLOR_BACK_CLOCK);
		lcd.printf(":");

		lcd.setTextColor(g_color_clock, COLOR_BACK_CLOCK);
		lcd.printf("%02d", time_count.get_sec());
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
		TimeSelector::PresetTime preset_time;
		struct {
			int x, y;
		} pos;
	} selector[] = {
		{ TimeSelector::PRESET_TIME_2_MIN,  {  30, 170 } },
		{ TimeSelector::PRESET_TIME_3_MIN,  { 130, 170 } },
		{ TimeSelector::PRESET_TIME_5_MIN,  { 230, 170 } },
		{ TimeSelector::PRESET_TIME_CUSTOM, {  80, 210 } },
	};
	const int NUM_SELECTOR = (sizeof(selector)/sizeof(selector[0]));

	TimeSelector::PresetTime pt_cur = g_time_selector.get_preset();
	int sec_count = g_time_selector.get_sec();

	for (int i = 0; i < NUM_SELECTOR; i++)
	{
		int x = selector[i].pos.x;
		int y = selector[i].pos.y;
		TimeSelector::PresetTime pt_temp = selector[i].preset_time;
		int sec_temp = TimeSelector::get_preset_sec(pt_temp);
		int min = sec_temp / 60;
		int sec = sec_temp % 60;

		lcd.setCursor(x, y);

		uint32_t color_back = (pt_cur == pt_temp) ? COLOR_BACK_SELECTED : COLOR_BACK_NOT_SELECT;
		lcd.setTextColor(COLOR_NORMAL, color_back);

		/* TODO: "CUSTOM" が選択されている場合は任意に設定された時間にする */
		if (pt_temp == TimeSelector::PRESET_TIME_CUSTOM)
		{
			min = 12;	/* TODO: shoud set to custom min. */
			sec = 34;	/* TODO: shoud set to custom sec. */
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
 * @brief 		アプリ動作状態の更新（イベント処理）：Idle
 * @return		新しい遷移先の状態、遷移先に変化がない場合は STAT_UNKNOWN
 */
int changestat_Idle(int event)
{
	int stat_new = STAT_UNKNOWN;

	switch(event)
	{
	case EVT_BTN_B_PRESSED:
		stat_new =  STAT_MEASURING;
		break;

	default:
		/* (do nothing) */
		break;
	}

	return stat_new;
}


/****************************************************************************
 * @brief 		アプリ動作状態処理：Idle
 * @param [in]	event - この状態内で処理するイベント：EVT_XXX
 */
void procstat_Idle(int event)
{
	static bool flag_blink;
	static unsigned long prev_msec;
	unsigned long msec;

	if (event == EVT_INIT)
	{
		g_time_count.reset();
		prev_msec = millis();
		flag_blink = true;
	}

	/* アラームの選択操作 */
	static const struct {
		TimeSelector::PresetTime preset_time;
	} preset_select_tab[] = {
		{ TimeSelector::PRESET_TIME_2_MIN  },
		{ TimeSelector::PRESET_TIME_3_MIN  },
		{ TimeSelector::PRESET_TIME_5_MIN  },
		{ TimeSelector::PRESET_TIME_CUSTOM },
	};
	static const int NUM_PT_TAB = (sizeof(preset_select_tab) / sizeof(preset_select_tab)[0]);
	int idx_pt;
	TimeSelector::PresetTime pt_cur = g_time_selector.get_preset();

	for (idx_pt = 0; idx_pt < NUM_PT_TAB; idx_pt++)
	{
		if (preset_select_tab[idx_pt].preset_time == pt_cur)
		{
			break;
		}
	}

	if (idx_pt < NUM_PT_TAB)
	{
		int dir = 0;
		if (event == EVT_BTN_A_PRESSED)
		{ /* Aボタン押し → 左向きの選択 */
			dir = -1;
		}
		else if (event == EVT_BTN_C_PRESSED)
		{ /* Cボタン押し → 右向きの選択 */
			dir = 1;
		}

		idx_pt = (idx_pt + NUM_PT_TAB + dir) % NUM_PT_TAB;
		TimeSelector::PresetTime pt_next = preset_select_tab[idx_pt].preset_time;
		g_time_selector.set_preset(pt_next);
	}

	msec = millis();
	if (300 <= (msec - prev_msec))
	{
		prev_msec = msec;
		flag_blink = flag_blink ? false : true;
	}

	/* 現在選択されているアラーム時間を計測時間表示のところに出す */
	g_color_clock = flag_blink ? COLOR_NORMAL : COLOR_BACK_OFF;
	TimeCount tc_temp;
	tc_temp.set_time(TimeSelector::get_preset_sec(pt_cur));
	drawTime(tc_temp);
}


/****************************************************************************
 * @brief 		アプリ動作状態の更新（イベント処理）：Measuring
 * @return		新しい遷移先の状態
  * @return		新しい遷移先の状態、遷移先に変化がない場合は STAT_UNKNOWN
 */
int changestat_Measuring(int event)
{
	int stat_new = STAT_UNKNOWN;

	switch(event)
	{
	case EVT_BTN_B_PRESSED:
		stat_new =  STAT_STOPPED;
		break;

	case EVT_TIME_EXPIRED:
		stat_new =  STAT_STOPPED;
		break;

	default:
		/* (do nothing) */
		break;
	}

	return stat_new;
}


/****************************************************************************
 * @brief 		アプリ動作状態処理：Measuring
 * @param [in]	event - この状態内で処理するイベント：EVT_XXX
 */
void procstat_Measuring(int event)
{
	static bool flag_blink;
	static unsigned long prev_msec;
	unsigned long msec;

	if (event == EVT_INIT)
	{
		g_color_clock = COLOR_NORMAL;
		prev_msec = millis();
		flag_blink = true;
		g_alarm_manager.set_alarm(g_time_selector.get_sec());
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
 * @brief 		アプリ動作状態の更新（イベント処理）：Stop
  * @return		新しい遷移先の状態、遷移先に変化がない場合は STAT_UNKNOWN
 */
int changestat_Stop(int event)
{
	int stat_new = STAT_UNKNOWN;

	switch(event)
	{
	case EVT_BTN_B_PRESSED:
		stat_new =  STAT_IDLE;
		break;

	default:
		/* (do nothing) */
		break;
	}

	return stat_new;
}


/****************************************************************************
 * @brief 		アプリ動作状態処理：Stop
 * @param [in]	event - この状態内で処理するイベント：EVT_XXX
 */
void procstat_Stopped(int event)
{
	static bool flag_blink;
	static unsigned long prev_msec;
	static int prev_step;
	unsigned long msec;

	if (event == EVT_INIT)
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
 * @param [in]	event - EVT_INIT:    状態遷移直後の初期化処理を実行する
 *						 EVT_NONE: 定常時の状態の処理を実行する
 * @note
 *   - 主に画面描画の処理を実行する。その他、時間の管理、音の出力等。
 *   - 他の状態から遷移してきた最初は EVT_INIT が渡される
 *   - 描画は lcd.startWrite() ～ lcd.endWrite() の間で実施することで高速化する。
 *     （SPIによるLCDへの画像データ転送をまとめて行うので省電力にもなる（はず））
 *     - この間にいる時は他の SPIデバイスは使えない。
 *       （→ それでも他の SPIデバイスを使いたい場合は LCD.beginTransaction()を利用する）
 */
void proc_state(int state, int event)
{
	lcd.startWrite();

	switch(state)
	{
	case STAT_IDLE:
		procstat_Idle(event);
		break;

	case STAT_MEASURING:
		procstat_Measuring(event);
		break;

	case STAT_STOPPED:
		procstat_Stopped(event);
		break;
	}

	/* バッテリーの状態（充電中、バッテリーレベル） */
	draw_power_status();

	/* タイマー時間の選択肢 */
	draw_timer_select();

	lcd.endWrite();
}


/****************************************************************************
 * @brief 		イベントを生成する
 * @return 		生成したイベント
 */
int generate_event()
{
	int event = EVT_NONE;

	if (M5.BtnA.wasPressed())
	{
		event = EVT_BTN_A_PRESSED;
	}
	else if (M5.BtnB.wasPressed())
	{
		event = EVT_BTN_B_PRESSED;
	}
	else if (M5.BtnC.wasPressed())
	{
		event = EVT_BTN_C_PRESSED;
	}
	else if (g_alarm_manager.is_expired())
	{
		g_alarm_manager.reset();
		event = EVT_TIME_EXPIRED;
	}
	else
	{
		event = EVT_NONE;
	}	

	return event;
}



/****************************************************************************
 * @brief 		状態を更新する
 * @param [in]	state - 現在の状態
 * @return 		更新後の状態
 */
int change_state(int state, int event)
{
	int state_new = STAT_UNKNOWN;

	switch(state)
	{
	case STAT_IDLE:
		state_new = changestat_Idle(event);
		break;

	case STAT_MEASURING:
		state_new = changestat_Measuring(event);
		break;
		
	case STAT_STOPPED:
		state_new = changestat_Stop(event);
		break;
		
	default:
		/* （ここには来ない） */
		break;
	}

	/* 遷移先に変化が無い場合 */
	if (state_new == STAT_UNKNOWN)
	{
		state_new = state;
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
	int event = EVT_INIT;

	/* ボタン入力の状態を更新する */
	M5.update();

	/* イベントを生成する */
	event = generate_event();

	/* アプリ動作状態を更新する */
	state = change_state(state, event);

	/* アプリ動作状態に変化があったら、新しい状態の初期化処理を走らせる */
	if (prev_state != state)
	{
		event = EVT_INIT;
	}

	/* 現在のアプリ動作状態を実行する */
	proc_state(state, event);

	prev_state = state;
}
