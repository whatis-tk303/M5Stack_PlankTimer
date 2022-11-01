#include <M5Stack.h>
#include <M5StackUpdater.h>

#define LGFX_AUTODETECT // 自動認識 (M5Stack, M5StickC/CPlus, ODROID-GO, TTGO T-Watch, TTGO T-Wristband, LoLin D32 Pro, ESP-WROVER-KIT)
#include <LovyanGFX.hpp>

LGFX lcd_real;
LGFX_Sprite sprite1(&lcd_real);		/* 分割描画バッファ１ */
LGFX_Sprite sprite2(&lcd_real);		/* 分割描画バッファ２ */
LGFX_Sprite &lcd = sprite1;

/* アプリ動作状態 */
enum {
	STAT_UNKNOWN,			/* ダミー状態（状態の実体は無い） */
	STAT_IDLE,				/* 設定時間選択                   */
	STAT_MEASURING,			/* 計測中                         */
	STAT_STOPPED,			/* 計測終了                       */
	STAT_CUSTOM_SETTING,	/* カスタム設定時間の変更         */
};

/* イベント */
enum {
	EVT_NONE,
	EVT_INIT,
	EVT_BTN_A_PRESSED,
	EVT_BTN_B_PRESSED,
	EVT_BTN_C_PRESSED,
	EVT_TIME_EXPIRED,
	EVT_BTN_A_RELEASED,
	EVT_BTN_B_RELEASED,
	EVT_BTN_C_RELEASED,
	EVT_BTN_A_LONG_PRESSED,
	EVT_BTN_B_LONG_PRESSED,
	EVT_BTN_C_LONG_PRESSED,
};

/* definition of colors */
static const uint32_t COLOR_NORMAL			= 0x00FF88U;	/* 通常の文字色                   */
static const uint32_t COLOR_STOP			= 0xFF0000U; 	/* タイマー停止時の文字色         */
static const uint32_t COLOR_BACK_OFF		= 0x000000U;	/* 通常の背景色                   */
static const uint32_t COLOR_BACK_CLOCK		= 0x002244U;	/* タイマーの背景色               */
static const uint32_t COLOR_BACK_SELECTED	= 0x999900U;	/* 選択されている設定値の背景色   */
static const uint32_t COLOR_BACK_NOT_SELECT	= 0x553311U;	/* 選択されていない設定値の背景色 */
static const uint32_t COLOR_CUSTOM_SETTING	= 0x999900U;	/* カスタム設定時の文字色         */
static const uint32_t COLOR_BATT_CHARGING	= 0x990000U;	/* バッテリー充電中の文字色       */

/** 設定時間の色 */
unsigned long g_color_clock;

/** カスタム設定時間 [sec] */
unsigned long g_custom_time;
bool g_is_long_pressed;

/** ブリンク表示用タイミングフラグ*/
bool g_flag_blink;

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
		case PRESET_TIME_CUSTOM:	sec = g_custom_time;	break;
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
void draw_time(LGFX_Sprite &sprite, int y_offset, const TimeCount &time_count, bool light = true) {
	sprite.setCursor(10, 40 - y_offset);
	sprite.setFont(&fonts::Font7);
	sprite.setTextSize(2.2);

	if (light)
	{
		sprite.setTextColor(g_color_clock, COLOR_BACK_CLOCK);
		sprite.printf("%02d:%02d", time_count.get_min(), time_count.get_sec());
	}
	else
	{
		sprite.setTextColor(g_color_clock, COLOR_BACK_CLOCK);
		sprite.printf("%02d", time_count.get_min());

		sprite.setTextColor(COLOR_BACK_OFF, COLOR_BACK_CLOCK);
		sprite.printf(":");

		sprite.setTextColor(g_color_clock, COLOR_BACK_CLOCK);
		sprite.printf("%02d", time_count.get_sec());
	}
}


/****************************************************************************
 * @brief 		バッテリー状態を描画する
 */
void draw_power_status(LGFX_Sprite &sprite, int y_offset)
{
	sprite.setCursor(2, 2 - y_offset);
	sprite.setFont(&fonts::FreeSansBold9pt7b);
	sprite.setTextSize(1.0);
	sprite.setTextColor(COLOR_NORMAL, COLOR_BACK_OFF);

	if (M5.Power.canControl())
	{
		const char *str_charge = "???";
		int8_t bat_level = -1;
		uint32_t color;

		if (M5.Power.isCharging())
		{
			str_charge = "Charging";
			color = COLOR_BATT_CHARGING;
		}
		else
		{
			str_charge = M5.Power.isChargeFull() ? "BattFULL" : "Battery";
			color = COLOR_NORMAL;
		}

		bat_level = M5.Power.getBatteryLevel();

		sprite.setTextColor(color, COLOR_BACK_OFF);
		sprite.printf("%8s:%03d%%", str_charge, bat_level);
	}
	else
	{
		sprite.setTextColor(COLOR_BACK_CLOCK, COLOR_BACK_OFF);
		sprite.printf("%8s:---%%", "Battery");
	}
}


/****************************************************************************
 * @brief 		設定時間の選択肢を描画する
 */
void draw_timer_select(LGFX_Sprite &sprite, int y_offset)
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

		sprite.setCursor(x, y - y_offset);

		uint32_t color_back = (pt_cur == pt_temp) ? COLOR_BACK_SELECTED : COLOR_BACK_NOT_SELECT;
		sprite.setTextColor(COLOR_NORMAL, color_back);

		/* TODO: "CUSTOM" が選択されている場合は任意に設定された時間にする */
		if (pt_temp == TimeSelector::PRESET_TIME_CUSTOM)
		{
			sprite.setFont(&fonts::FreeSansBold9pt7b);
			sprite.setTextSize(1.4);
			sprite.printf("CUSTOM");
		}

		sprite.setFont(&fonts::Font7);
		sprite.setTextSize(0.5);
		sprite.printf(" %02d:%02d ", min, sec);
	}
}


/****************************************************************************
 * @brief 		カスタム時間設定の操作ガイドを表示する
 */
void draw_custom_ope_guid(LGFX_Sprite &sprite, int y_offset)
{
	sprite.setFont(&fonts::FreeSansBold9pt7b);
	sprite.setTextSize(2.0);
	sprite.setTextColor(COLOR_NORMAL, COLOR_BACK_OFF);
	sprite.setCursor(40, 190 - y_offset);
	sprite.printf("[-]   [Start]   [+]");
}


/****************************************************************************
 * @brief 		アプリ動作状態の更新（イベント処理）：Idle
 * @return		新しい遷移先の状態、遷移先に変化がない場合は STAT_UNKNOWN
 */
int changestat_Idle(int event)
{
	int stat_new = STAT_UNKNOWN;
	bool is_selected_custom = (g_time_selector.get_preset() == TimeSelector::PRESET_TIME_CUSTOM);

	switch(event)
	{
	case EVT_BTN_B_PRESSED:
		stat_new = is_selected_custom ? STAT_CUSTOM_SETTING : STAT_MEASURING;
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
	static unsigned long prev_msec;
	unsigned long msec;

	if (event == EVT_INIT)
	{
		g_time_count.reset();
		prev_msec = millis();
		g_flag_blink = true;
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
		g_flag_blink = g_flag_blink ? false : true;
	}
}


/****************************************************************************
 * @brief 		アプリ動作状態の描画：Idle
 * @param [in]	sprite   - 描画対象Sprite
 * @param [in]	y_offset - 画面分割描画のためのY方向オフセット（この分を引いて（上にずらして）描画する）
 * @return		新しい遷移先の状態、遷移先に変化がない場合は STAT_UNKNOWN
 */
void drawstat_Idle(LGFX_Sprite &sprite, int y_offset)
{
	/* 現在選択されているアラーム時間を計測時間表示のところに出す */
	g_color_clock = g_flag_blink ? COLOR_NORMAL : COLOR_BACK_OFF;
	TimeSelector::PresetTime pt_cur = g_time_selector.get_preset();
	TimeCount tc_temp;
	tc_temp.set_time(TimeSelector::get_preset_sec(pt_cur));
	draw_time(sprite, y_offset, tc_temp);
}


/****************************************************************************
 * @brief 		アプリ動作状態の更新（イベント処理）：Measuring
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
	static unsigned long expired_msec;
	static unsigned long prev_msec;

	unsigned long msec;

	if (event == EVT_INIT)
	{
		g_color_clock = COLOR_NORMAL;
		prev_msec = expired_msec = millis();
		expired_msec += 1000;
		g_flag_blink = true;
		g_alarm_manager.set_alarm(g_time_selector.get_sec());
	}

	msec = millis();

	if (expired_msec <= msec)
	{
		expired_msec += 1000;
		g_time_count.countUp();
	}

	prev_msec = expired_msec - 1000;
	unsigned long past_time = msec - prev_msec;
	g_flag_blink = (past_time < 500) ? true : false;
}


/****************************************************************************
 * @brief 		アプリ動作状態の描画：Measuring
 * @param [in]	sprite   - 描画対象Sprite
 * @param [in]	y_offset - 画面分割描画のためのY方向オフセット（この分を引いて（上にずらして）描画する）
 * @return		新しい遷移先の状態、遷移先に変化がない場合は STAT_UNKNOWN
 */
void drawstat_Measuring(LGFX_Sprite &sprite, int y_offset)
{
	draw_time(sprite, y_offset, g_time_count, g_flag_blink);
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
	/* 「CUSTOM」が選択されている場合にすぐに計測に遷移しないようにするため RELEASED で遷移させる
	 *	（他の状態中の場合でも弊害は無いので同じ操作になるようにする） */
	case EVT_BTN_B_RELEASED:
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
	static unsigned long prev_msec;
	static int prev_step;
	unsigned long msec;

	if (event == EVT_INIT)
	{
		g_color_clock = COLOR_STOP;
		prev_msec = millis();
		g_flag_blink = true;
		prev_step = -1;
	}

	msec = millis();
	unsigned long past = msec - prev_msec;
	if (500 <= past)
	{
		prev_msec = msec;
		g_flag_blink = g_flag_blink ? false : true;
	}

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
 * @brief 		アプリ動作状態の描画：Stopped
 * @param [in]	sprite   - 描画対象Sprite
 * @param [in]	y_offset - 画面分割描画のためのY方向オフセット（この分を引いて（上にずらして）描画する）
 * @return		新しい遷移先の状態、遷移先に変化がない場合は STAT_UNKNOWN
 */
void drawstat_Stopped(LGFX_Sprite &sprite, int y_offset)
{
	g_color_clock = g_flag_blink ? COLOR_STOP : COLOR_NORMAL;
	draw_time(sprite, y_offset, g_time_count);
}

/****************************************************************************
 * @brief 		アプリ動作状態の更新（イベント処理）：Stop
 * @return		新しい遷移先の状態、遷移先に変化がない場合は STAT_UNKNOWN
 */
int changestat_CustomSetting(int event)
{
	static const unsigned long INTERVAL_LONG_PRESS = 300;	/* for interval of button pressed while long time [msec] */
	static const unsigned long DIFF_TIME_PRESSED = 60;
	static const unsigned long DIFF_TIME_LONG_PRESSED = 600;
	static const signed long LIMIT_TIME_LOWER = (1 * 60);	/* lower limit:  1 [min] */
	static const signed long LIMIT_TIME_UPPER = (99 * 60);	/* upper limit: 99 [min] */

	static unsigned long last_millis = 0;
	unsigned long now_millis;
	int stat_new = STAT_UNKNOWN;
	signed long updated_time = (signed long) g_custom_time;

	g_is_long_pressed = false;
	now_millis = millis();

	switch(event)
	{
	case EVT_BTN_B_PRESSED:
		stat_new =  STAT_MEASURING;
		break;

	case EVT_BTN_A_PRESSED:
		updated_time -= DIFF_TIME_PRESSED;
		break;

	case EVT_BTN_A_LONG_PRESSED:
		g_is_long_pressed = true;
		if (INTERVAL_LONG_PRESS < (now_millis - last_millis)) {
			updated_time -= DIFF_TIME_LONG_PRESSED;
			last_millis = now_millis;
		}
		break;

	case EVT_BTN_C_PRESSED:
		updated_time += DIFF_TIME_PRESSED;
		break;

	case EVT_BTN_C_LONG_PRESSED:
		g_is_long_pressed = true;
		if (INTERVAL_LONG_PRESS < (now_millis - last_millis)) {
			updated_time += DIFF_TIME_LONG_PRESSED;
			last_millis = now_millis;
		}
		break;

	default:
		/* (do nothing) */
		break;
	}

	if (updated_time < LIMIT_TIME_LOWER) {
		updated_time = LIMIT_TIME_LOWER;
	}
	else if (LIMIT_TIME_UPPER < updated_time) {
		updated_time = LIMIT_TIME_UPPER;
	}

	g_custom_time = (unsigned long)updated_time;

	return stat_new;
}


/****************************************************************************
 * @brief 		アプリ動作状態処理：CustomSetting
 * @param [in]	event - この状態内で処理するイベント：EVT_XXX
 */
void procstat_CustomSetting(int event)
{
	static unsigned long prev_msec;
	unsigned long msec;

	if (event == EVT_INIT)
	{
		g_color_clock = COLOR_STOP;
		prev_msec = millis();
		g_flag_blink = true;
	}

	msec = millis();
	unsigned long past = msec - prev_msec;
	if (500 <= past)
	{
		prev_msec = msec;
		g_flag_blink = g_flag_blink ? false : true;
	}
}


/****************************************************************************
 * @brief 		アプリ動作状態の描画：CustomSetting
 * @param [in]	sprite   - 描画対象Sprite
 * @param [in]	y_offset - 画面分割描画のためのY方向オフセット（この分を引いて（上にずらして）描画する）
 * @return		新しい遷移先の状態、遷移先に変化がない場合は STAT_UNKNOWN
 */
void drawstat_CustomSetting(LGFX_Sprite &sprite, int y_offset)
{
	/* カスタム設定時間を表示する（黄色文字、点滅なし） */
	g_color_clock = COLOR_CUSTOM_SETTING;
	TimeCount tc_temp;
	TimeSelector::PresetTime pt_cur = g_time_selector.get_preset();
	tc_temp.set_time(TimeSelector::get_preset_sec(pt_cur));
	draw_time(sprite, y_offset, tc_temp);
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
	void (*p_drawstat)(LGFX_Sprite &, int);

	switch(state)
	{
	case STAT_IDLE:
		procstat_Idle(event);
		p_drawstat = drawstat_Idle;
		break;

	case STAT_MEASURING:
		procstat_Measuring(event);
		p_drawstat = drawstat_Measuring;
		break;

	case STAT_STOPPED:
		procstat_Stopped(event);
		p_drawstat = drawstat_Stopped;
		break;

	case STAT_CUSTOM_SETTING:
		procstat_CustomSetting(event);
		p_drawstat = drawstat_CustomSetting;
		break;
	}
	
	lcd_real.startWrite();

	/* 画面描画（分割描画のため分割数分を繰り返している） */
	for (int i = 0; i <= 1; i++) {
		LGFX_Sprite &sprite = (i == 0) ? sprite1 : sprite2;
		int y_offset = (i == 0) ? 0 : 120;

		/* 各状態を描画する */
		p_drawstat(sprite, y_offset);

		/* バッテリーの状態（充電中、バッテリーレベル） */
		draw_power_status(sprite, y_offset);

		/* タイマー時間の選択肢 */
		sprite.fillRect(0, 170 - y_offset, 320, (240-170), COLOR_BACK_OFF);
		if (state == STAT_CUSTOM_SETTING)
		{
			draw_custom_ope_guid(sprite, y_offset);
		}
		else
		{
			draw_timer_select(sprite, y_offset);
		}

		sprite.pushSprite(0, y_offset);
	}

	lcd_real.endWrite();
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
	else if (M5.BtnB.wasReleased())
	{
		event = EVT_BTN_B_RELEASED;
	}
	else if (M5.BtnA.pressedFor(1000))
	{ /* 左のボタン（A）が1秒間長押しされた */
		event = EVT_BTN_A_LONG_PRESSED;
	}	
	else if (M5.BtnB.pressedFor(1000))
	{ /* 真ん中のボタン（B）が1秒間長押しされた */
		event = EVT_BTN_B_LONG_PRESSED;
	}	
	else if (M5.BtnC.pressedFor(1000))
	{ /* 右のボタン（C）が1秒間長押しされた */
		event = EVT_BTN_C_LONG_PRESSED;
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

	case STAT_CUSTOM_SETTING:
		state_new = changestat_CustomSetting(event);
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

	/* LCD（デバイス）の初期化 */
	lcd_real.init();
	lcd_real.setRotation(1);
	lcd_real.setBrightness(64);
	lcd_real.setColorDepth(16);  // RGB565の16ビットに設定

	/* バッファ画面（LCDと同サイズを2分割したもの）の初期化 */
	/* 1枚目のスプライトの初期化 */
	sprite1.setColorDepth(lcd_real.getColorDepth());
	sprite1.createSprite(lcd_real.width(), (lcd_real.height() / 2));
	/* 2枚目のスプライトの初期化 */
	sprite2.setColorDepth(lcd_real.getColorDepth());
	sprite2.createSprite(lcd_real.width(), (lcd_real.height() / 2));

	Serial.begin(115200);

	M5.Speaker.begin();
	M5.Power.begin();

	/* カスタム設定時間：デフォルト 10分 */
	g_custom_time = (10 * 60);
}


/****************************************************************************
 * @brief 		Arduino関数：定常動作時の処理
 */
void loop() {
	static int state = STAT_IDLE;
	static int prev_state = STAT_UNKNOWN;
	int event; // = EVT_INIT;

	/* ボタン入力の状態を更新する */
	M5.update();

	/* イベントを生成する（ボタン押し、タイマー満了、etc.） */
	event = generate_event();

	/* イベントに応じてアプリ動作状態を更新する */
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
