#include <M5Stack.h>
#include <M5StackUpdater.h>

#define LGFX_AUTODETECT // 自動認識 (M5Stack, M5StickC/CPlus, ODROID-GO, TTGO T-Watch, TTGO T-Wristband, LoLin D32 Pro, ESP-WROVER-KIT)
#include <LovyanGFX.hpp>

const int NUM_DIVIDE_OFFSCREEN = 2;
LGFX lcd_real;
LGFX_Sprite sprite1(&lcd_real);		/* 分割描画バッファ１ */
LGFX_Sprite sprite2(&lcd_real);		/* 分割描画バッファ２ */

/** アプリ動作状態 */
typedef enum {
	STAT_UNKNOWN,			/* ダミー状態（状態の実体は無い） */
	STAT_IDLE,				/* 設定時間選択                   */
	STAT_MEASURING,			/* 計測中                         */
	STAT_STOPPED,			/* 計測終了                       */
	STAT_CUSTOM_SETTING,	/* カスタム設定時間の変更         */
} State_t;


/** アプリケーションのイベント */
typedef enum {
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
} Event_t;

/** definition of colors */
static const uint32_t COLOR_NORMAL			= 0x00FF88U;	/* 通常の文字色                   */
static const uint32_t COLOR_STOP			= 0xFF0000U; 	/* タイマー停止時の文字色         */
static const uint32_t COLOR_BACK_OFF		= 0x000000U;	/* 通常の背景色                   */
static const uint32_t COLOR_BACK_CLOCK		= 0x002244U;	/* タイマーの背景色               */
static const uint32_t COLOR_BACK_SELECTED	= 0x999900U;	/* 選択されている設定値の背景色   */
static const uint32_t COLOR_BACK_NOT_SELECT	= 0x553311U;	/* 選択されていない設定値の背景色 */
static const uint32_t COLOR_CUSTOM_SETTING	= 0x999900U;	/* カスタム設定時の文字色         */
static const uint32_t COLOR_BATT_CHARGING	= 0x990000U;	/* バッテリー充電中の文字色       */


/****************************************************************************
 * @brief	プリセット時間の種類を特定するID
 */
typedef enum {
	PRESET_TIME_2_MIN,
	PRESET_TIME_3_MIN,
	PRESET_TIME_5_MIN,
	PRESET_TIME_CUSTOM,
} PresetTimeID;


/****************************************************************************
 * @brief	カウント時間クラス
 * @note	時間設定の保持、計測中の時間変更
 */
class CounterTime
{
public:
	/** 保持している秒数から分と秒を作るサポートクラス */
	struct ClockTime
	{
		/* constructor */
		ClockTime(unsigned long sec_total)
		{
			min = sec_total / 60;
			sec = sec_total % 60;
		}

		unsigned long min;
		unsigned long sec;
	};

	/** constructor */
	CounterTime(unsigned long sec) : sec_(sec) {}

	/**/
	unsigned long get_sec() const
	{
		return sec_;
	}

	/**/
	ClockTime get_clock_time() const
	{
		ClockTime ct(sec_);
		return ct;
	}

	/**/
	void increase_sec(unsigned long sec_diff, unsigned long upper_limit = (60 * 60 * 24))
	{
		sec_ += sec_diff;
		if (upper_limit < sec_)
		{
			sec_ = upper_limit;
		}
	}

	/**/
	void decrease_sec(unsigned long sec_diff, unsigned long lower_limit = 0)
	{
		sec_ = ((sec_diff < sec_) ? (sec_ - sec_diff) : 0);
		if (sec_ < lower_limit)
		{
			sec_ = lower_limit;
		}
	}

private:
	unsigned long sec_;	
};


/****************************************************************************
 * @brief	プリセット時間クラス
 * @note	プリセット時間の保持、IDとの紐づけ、カスタム設定時間の識別と時間変更
 */
class PresetTime
{
public:
	/* constructor */
	PresetTime(PresetTimeID id, unsigned long sec, bool is_custom = false)
	 : id_(id), counter_time_(CounterTime(sec)), is_custom_(is_custom)
	{}

	/**/
	PresetTimeID get_id() const
	{
		return id_;
	}

	/**/
	bool is_custom()
	{
		return is_custom_;
	}

	/**/
	const CounterTime & get_time() const
	{
		return counter_time_;
	}

	/**/
	void set_custom_time(unsigned long sec)
	{
		if (is_custom_)
		{
			counter_time_ = CounterTime(sec);
		}
	}

private:
	PresetTimeID id_;
	CounterTime counter_time_;
	bool is_custom_;
};


/****************************************************************************
 * @brief	設定時間を選択するクラス
 * @note	プリセット時間を並び順に保持する、プリセット選択の変更操作、選択されているプリセットの時間を取得
 */
class TimeSelector
{
public:
	/* constructor */
	TimeSelector() : index_preset_(0) {}

	/** プリセット時間を追加登録する */
	void add(PresetTime pt)
	{
		selector_table_.push_back(pt);
	}

	/** 現在選択中のプリセットを取得する */
	const PresetTime & get_selected_preset() const
	{
		return selector_table_[index_preset_];
	}

	/** 次の選択肢に切り替える */
	void select_prev()
	{
		index_preset_ = (index_preset_ + selector_table_.size() - 1) % selector_table_.size();
	}

	/** 前の選択肢に切り替える */
	void select_next()
	{
		index_preset_ = (index_preset_ + 1) % selector_table_.size();
	}

	/** 現在選択中のプリセットにカスタム時間が選択されているかを問い合わせる */
	bool is_selected_custom()
	{
		return selector_table_[index_preset_].is_custom();
	}

	/** 指定されたIDのプリセットを取得する */
	PresetTime & get_preset(PresetTimeID id)
	{
		auto result = std::find_if(selector_table_.begin(), selector_table_.end(),
									[id](PresetTime &x) { return x.get_id() == id; });
		return ((result == selector_table_.end()) ? selector_table_[0] : *result);
	}

private:
	std::vector<PresetTime> selector_table_;
	int index_preset_;
};


/****************************************************************************
 * @brief	時間計測の管理クラス
 * @note	時間計測アプリケーションの操作、リセット、時間の設定、タイマー満了のチェック
 */
class AlarmManager
{
public:
	/* constructor */
	AlarmManager() : alarm_time_(0), time_count_(0)
	{
		reset();
	}

	/**/
	void reset()
	{
		alarm_enable = false;
		time_count_ = CounterTime(0);
	}

	/**/
	void set_alarm(const CounterTime & time)
	{
		alarm_enable = true;
		alarm_time_ = time;
	}

	/**/
	const CounterTime & get_measuring_time() const
	{
		return time_count_;
	}

	/**/
	void count_up(unsigned long sec)
	{
		time_count_.increase_sec(sec);
	}

	/**/
	bool is_expired()
	{
		unsigned long  current_sec = time_count_.get_sec();
		unsigned long  target_sec = alarm_time_.get_sec();
		return ((alarm_enable) && (target_sec <= current_sec));
	}

private:
	CounterTime alarm_time_;		/* アラーム時間の設定 */
	CounterTime time_count_;		/* 計測中の時間（参照） */
	bool alarm_enable;
};


/****************************************************************************
 * @brief 	経過時間計測クラス
 * @note	簡単に時間間隔を計測できるようにするためのサポートユーティリティ
 * 			Arduinoプログラミングでは mills() 関数でミリ秒単位の経過時間が
 * 			取得できるのでそれを利用する。
 */
class ClockInterval
{
public:
	/* constructor*/
	ClockInterval(unsigned long (msec_func)(void))
	{
		get_cur_msec_ = msec_func;
		msec_prev_ = 0;
	}

	void mark()
	{
		msec_prev_ = get_cur_msec_();
	}

	bool is_past(unsigned long interval)
	{
		unsigned long msec_now = get_cur_msec_();
		unsigned long msec_expired = msec_prev_ + interval;

		return (msec_expired <= msec_now);
	}

private:
	unsigned long (*get_cur_msec_)(void);	/* 経過時間を msec で返す関数へのポインタ */
	unsigned long msec_prev_;
};


/* カウントダウンタイマーのドメインオブジェクト */
AlarmManager g_alarm_manager;
TimeSelector g_time_selector;

/** 設定時間の色 */
unsigned long g_color_clock;

/** ブリンク表示用タイミングフラグ*/
bool g_flag_blink;


/****************************************************************************
 * @brief 		計測時間を描画する
 * @param [in]	time_count - 計測時間
 * @param [in]	light      - 点滅状態（true: 点灯）
 */
void draw_time(LGFX_Sprite &sprite, int y_offset, const CounterTime &time, bool light = true) {
	sprite.setCursor(5, 50 - y_offset);
	sprite.setFont(&fonts::Font7);
	sprite.setTextSize(1.8);

	CounterTime::ClockTime ct = time.get_clock_time();

	if (light)
	{
		sprite.setTextColor(g_color_clock, COLOR_BACK_CLOCK);
		sprite.printf("  %02d:%02d ", (int)ct.min, (int)ct.sec);
	}
	else
	{
		sprite.setTextColor(g_color_clock, COLOR_BACK_CLOCK);
		sprite.printf("  %02d", (int)ct.min);

		sprite.setTextColor(COLOR_BACK_OFF, COLOR_BACK_CLOCK);
		sprite.printf(":");

		sprite.setTextColor(g_color_clock, COLOR_BACK_CLOCK);
		sprite.printf("%02d ", (int)ct.sec);
	}
}


/****************************************************************************
 * @brief 		計測中の進捗状況を描画する
 */
void draw_progress_measure(LGFX_Sprite &sprite, int y_offset, int x_progress) {
	int32_t w = lcd_real.width();
	int32_t h = 16;

	sprite.fillRect(0,  27 - y_offset, w, h, COLOR_NORMAL);
	sprite.fillRect(0, 144 - y_offset, w, h, COLOR_NORMAL);

	sprite.fillRect(x_progress,  27 - y_offset, 20, h, COLOR_BACK_OFF);
	sprite.fillRect(x_progress, 144 - y_offset, 20, h, COLOR_BACK_OFF);
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
	static const struct {
		PresetTimeID id;
		struct {
			int x, y;
		} pos;
	} selector_items[] = {
		{ PRESET_TIME_2_MIN,  {  30, 170 } },
		{ PRESET_TIME_3_MIN,  { 130, 170 } },
		{ PRESET_TIME_5_MIN,  { 230, 170 } },
		{ PRESET_TIME_CUSTOM, {  80, 210 } },
	};

	static const int NUM_SELECTOR_ITEM = (sizeof(selector_items)/sizeof(selector_items[0]));

	const PresetTime &pt_cur = g_time_selector.get_selected_preset();

	for (int i = 0; i < NUM_SELECTOR_ITEM; i++)
	{
		int x = selector_items[i].pos.x;
		int y = selector_items[i].pos.y;
		PresetTimeID id = selector_items[i].id;
		PresetTime & preset = g_time_selector.get_preset(id);
		CounterTime time = preset.get_time();
		CounterTime::ClockTime ct = time.get_clock_time();

		sprite.setCursor(x, y - y_offset);

		uint32_t color_back = (pt_cur.get_id() == id) ? COLOR_BACK_SELECTED : COLOR_BACK_NOT_SELECT;
		sprite.setTextColor(COLOR_NORMAL, color_back);

		if (id == PRESET_TIME_CUSTOM)
		{
			sprite.setFont(&fonts::FreeSansBold9pt7b);
			sprite.setTextSize(1.4);
			sprite.printf("CUSTOM");
		}

		sprite.setFont(&fonts::Font7);
		sprite.setTextSize(0.5);
		sprite.printf(" %02d:%02d ", (int)ct.min, (int)ct.sec);
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
 * @param [in]	event - 通知されたイベント
 * @return		新しい遷移先の状態、遷移先に変化がない場合は STAT_UNKNOWN
 */
State_t changestat_Idle(Event_t event)
{
	State_t stat_new = STAT_UNKNOWN;

	switch(event)
	{
	case EVT_BTN_A_PRESSED:	/* Aボタン押し → 左向きの選択 */
		g_time_selector.select_prev();
		/* (状態の遷移は無し) */
		break;

	case EVT_BTN_C_PRESSED:	/* Cボタン押し → 右向きの選択 */
		g_time_selector.select_next();
		/* (状態の遷移は無し) */
		break;

	case EVT_BTN_B_PRESSED:
		stat_new = g_time_selector.is_selected_custom() ? STAT_CUSTOM_SETTING : STAT_MEASURING;
		break;

	default:
		/* (do nothing) */
		break;
	}

	return stat_new;
}


/****************************************************************************
 * @brief 		アプリ動作状態処理：Idle
 * @param [in]	is_enter - この状態内に最初に入ってきたことを示す（この状態を初期化するために参照する）
 */
void procstat_Idle(bool is_enter)
{
	static ClockInterval interval(millis);

	if (is_enter)
	{
		interval.mark();
		g_flag_blink = true;
	}

	if (interval.is_past(300))
	{
		interval.mark();
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
	PresetTime pt_cur = g_time_selector.get_selected_preset();
	const CounterTime & ct = pt_cur.get_time();
	draw_time(sprite, y_offset, ct);
	draw_timer_select(sprite, y_offset);
}


/****************************************************************************
 * @brief 		アプリ動作状態の更新（イベント処理）：Measuring
 * @param [in]	event - 通知されたイベント
* @return		新しい遷移先の状態、遷移先に変化がない場合は STAT_UNKNOWN
 */
State_t changestat_Measuring(Event_t event)
{
	State_t stat_new = STAT_UNKNOWN;

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
 * @param [in]	is_enter - この状態内に最初に入ってきたことを示す（この状態を初期化するために参照する）
 */
void procstat_Measuring(bool is_enter)
{
	static ClockInterval interval(millis);

	if (is_enter)
	{
		g_alarm_manager.reset();
		interval.mark();
		g_flag_blink = true;
		PresetTime const & preset = g_time_selector.get_selected_preset();
		g_alarm_manager.set_alarm(preset.get_time());
	}

	g_flag_blink = interval.is_past(500) ? false : true;

	/* 1秒経過で1秒ずつカウントアップしていく */
	if (interval.is_past(1000))
	{
		interval.mark();
		g_alarm_manager.count_up(1);
	}
}


/****************************************************************************
 * @brief 		アプリ動作状態の描画：Measuring
 * @param [in]	sprite   - 描画対象Sprite
 * @param [in]	y_offset - 画面分割描画のためのY方向オフセット（この分を引いて（上にずらして）描画する）
 */
void drawstat_Measuring(LGFX_Sprite &sprite, int y_offset)
{
	static int x_progress = 0;

	x_progress = (x_progress + 30) % lcd_real.width();

	g_color_clock = COLOR_NORMAL;
	const CounterTime & time = g_alarm_manager.get_measuring_time();
	draw_time(sprite, y_offset, time, g_flag_blink);
	draw_progress_measure(sprite, y_offset, x_progress);
	draw_timer_select(sprite, y_offset);
}


/****************************************************************************
 * @brief 		アプリ動作状態の更新（イベント処理）：Stop
 * @param [in]	event - 通知されたイベント
 * @return		新しい遷移先の状態、遷移先に変化がない場合は STAT_UNKNOWN
 */
State_t changestat_Stop(Event_t event)
{
	State_t stat_new = STAT_UNKNOWN;

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
 * @param [in]	is_enter - この状態内に最初に入ってきたことを示す（この状態を初期化するために参照する）
 */
void procstat_Stopped(bool is_enter)
{
	static const uint16_t BUZZ_FREQUENCY = 880;
	static const uint32_t BUZZ_DURATION  = 50;

	static ClockInterval interval(millis);
	static ClockInterval interval_2nd_buzzer(millis);
	static int count_buzzer;

	if (is_enter)
	{
		interval.mark();

		interval_2nd_buzzer.mark();
		count_buzzer = 1;
		M5.Speaker.tone(BUZZ_FREQUENCY, BUZZ_DURATION);

		g_flag_blink = true;
	}

	if (interval.is_past(500))
	{
		interval.mark();
		g_flag_blink = g_flag_blink ? false : true;

		interval_2nd_buzzer.mark();
		count_buzzer += 1;
		M5.Speaker.tone(BUZZ_FREQUENCY, BUZZ_DURATION);
	}

	if ((count_buzzer == 1) && interval_2nd_buzzer.is_past(150))
	{
		count_buzzer = 0;
		M5.Speaker.tone(BUZZ_FREQUENCY, BUZZ_DURATION);
	}
}


/****************************************************************************
 * @brief 		アプリ動作状態の描画：Stopped
 * @param [in]	sprite   - 描画対象Sprite
 * @param [in]	y_offset - 画面分割描画のためのY方向オフセット（この分を引いて（上にずらして）描画する）
 */
void drawstat_Stopped(LGFX_Sprite &sprite, int y_offset)
{
	g_color_clock = g_flag_blink ? COLOR_STOP : COLOR_NORMAL;
	const CounterTime & time = g_alarm_manager.get_measuring_time();
	draw_time(sprite, y_offset, time);
	draw_timer_select(sprite, y_offset);
}

/****************************************************************************
 * @brief 		アプリ動作状態の更新（イベント処理）：CustomSetting
 * @param [in]	event - 通知されたイベント
 * @return		新しい遷移先の状態、遷移先に変化がない場合は STAT_UNKNOWN
 */
State_t changestat_CustomSetting(Event_t event)
{
	static const unsigned long INTERVAL_LONG_PRESS = 300;	/* for interval of button pressed while long time [msec] */
	static const unsigned long DIFF_TIME_PRESSED = 60;
	static const unsigned long DIFF_TIME_LONG_PRESSED = 600;
	static const unsigned long LOWER_LIMIT_SEC = (1 * 60);
	static const unsigned long UPPER_LIMIT_SEC = (99 * 60);

	static unsigned long last_millis = 0;
	unsigned long now_millis;
	State_t stat_new = STAT_UNKNOWN;

	/* カスタム時間の設定を変更する（増加） */
	auto increase_custom_time = [](unsigned long diff_sec){
		const PresetTime & custom_preset = g_time_selector.get_preset(PresetTimeID::PRESET_TIME_CUSTOM);
		CounterTime & time = (CounterTime &)custom_preset.get_time();
		time.increase_sec(diff_sec, UPPER_LIMIT_SEC);
	};

	/* カスタム時間の設定を変更する（減少） */
	auto decrease_custom_time = [](unsigned long diff_sec){
		const PresetTime & custom_preset = g_time_selector.get_preset(PresetTimeID::PRESET_TIME_CUSTOM);
		CounterTime & time = (CounterTime &)custom_preset.get_time();
		time.decrease_sec(diff_sec, LOWER_LIMIT_SEC);
	};

	now_millis = millis();

	switch(event)
	{
	case EVT_BTN_B_PRESSED:
		stat_new =  STAT_MEASURING;
		break;

	case EVT_BTN_A_PRESSED:
		decrease_custom_time(DIFF_TIME_PRESSED);
		break;

	case EVT_BTN_A_LONG_PRESSED:
		if (INTERVAL_LONG_PRESS < (now_millis - last_millis)) {
			decrease_custom_time(DIFF_TIME_LONG_PRESSED);
			last_millis = now_millis;
		}
		break;

	case EVT_BTN_C_PRESSED:
		increase_custom_time(DIFF_TIME_PRESSED);
		break;

	case EVT_BTN_C_LONG_PRESSED:
		if (INTERVAL_LONG_PRESS < (now_millis - last_millis)) {
			increase_custom_time(DIFF_TIME_LONG_PRESSED);
			last_millis = now_millis;
		}
		break;

	default:
		/* (do nothing) */
		break;
	}

	return stat_new;
}


/****************************************************************************
 * @brief 		アプリ動作状態処理：CustomSetting
  * @param [in]	is_enter - この状態内に最初に入ってきたことを示す（この状態を初期化するために参照する）
 */
void procstat_CustomSetting(bool is_enter)
{
	if (is_enter)
	{
		/* (do nothing) */
	}

	/* (do nothing) */
}


/****************************************************************************
 * @brief 		アプリ動作状態の描画：CustomSetting
 * @param [in]	sprite   - 描画対象Sprite
 * @param [in]	y_offset - 画面分割描画のためのY方向オフセット（この分を引いて（上にずらして）描画する）
 */
void drawstat_CustomSetting(LGFX_Sprite &sprite, int y_offset)
{
	/* カスタム設定時間を表示する（黄色文字、点滅なし） */
	g_color_clock = COLOR_CUSTOM_SETTING;
	PresetTime pt_cur = g_time_selector.get_selected_preset();
	const CounterTime & ct = pt_cur.get_time();
	draw_time(sprite, y_offset, ct);
	draw_custom_ope_guid(sprite, y_offset);
}


/****************************************************************************
 * @brief		現在のアプリ状態を描画する
 * @param [in]	p_drawstat - 各状態の描画関数
 * @note
 *   - 描画は LCD.startWrite() ～ LCD.endWrite() の間で実施することで
 * 		他のSPIデバイスへのアクセスをブロックする。
 *     - この間にいる時は他の SPIデバイスは使えない。
 *       （→ それでも他の SPIデバイスを使いたい場合は LCD.beginTransaction()を利用する）
 */
void draw_state(void (*p_drawstat)(LGFX_Sprite &, int))
{
	/* 画面描画（分割描画のため分割数分を繰り返している）
	 * オフスクリーンに描画してからLCDへ転送するｘ分割数 */
	int32_t width = lcd_real.width();
	int32_t height = lcd_real.height() / NUM_DIVIDE_OFFSCREEN;

	lcd_real.startWrite();

	for (int i = 0; i < NUM_DIVIDE_OFFSCREEN; i++) {
		LGFX_Sprite &sprite = (i == 0) ? sprite1 : sprite2;
		int y_offset = (i == 0) ? 0 : 120;

		sprite.fillRect(0, 0, width, height, COLOR_BACK_OFF);

		/* 各状態を描画する */
		p_drawstat(sprite, y_offset);

		/* バッテリーの状態（充電中、バッテリーレベル） */
		draw_power_status(sprite, y_offset);

		/* オフスクリーンに描画した内容を LCDへ転送する */
		sprite.pushSprite(0, y_offset);
	}

	lcd_real.endWrite();
}


/****************************************************************************
 * @brief		現在のアプリ状態の動作を実行する
 * @param [in]	state      - 実行するアプリ動作状態：STAT_XXX
 * @param [in]	is_changed - 状態が変化したことを示す
 */
void proc_state(int state, bool is_changed)
{
	void (*p_drawstat)(LGFX_Sprite &, int) = NULL;
	void (*p_procstat)(bool) = NULL;
	bool is_enter = is_changed;

	switch(state)
	{
	case STAT_IDLE:
		p_procstat = procstat_Idle;
		p_drawstat = drawstat_Idle;
		break;

	case STAT_MEASURING:
		p_procstat = procstat_Measuring;
		p_drawstat = drawstat_Measuring;
		break;

	case STAT_STOPPED:
		p_procstat = procstat_Stopped;
		p_drawstat = drawstat_Stopped;
		break;

	case STAT_CUSTOM_SETTING:
		p_procstat = procstat_CustomSetting;
		p_drawstat = drawstat_CustomSetting;
		break;
	}

	/* 現在の状態の処理 */
	if (p_procstat != NULL)
	{
		p_procstat(is_enter);
	}

	/* 現在の状態の描画 */
	if (p_drawstat != NULL)
	{
		draw_state(p_drawstat);
	}
}


/****************************************************************************
 * @brief 		イベントを生成する
 * @return 		生成したイベント。イベントの生成がなければ EVT_NONE
 */
Event_t generate_event()
{
	static const unsigned long SEC_LONG_PRESS = 1000;		/* 長押しを判定する時間 [ms] */

	/* 左のボタン（A）が押された */
	if (M5.BtnA.wasPressed()) { return EVT_BTN_A_PRESSED; }

	 /* 真ん中のボタン（B）が押された */
	if (M5.BtnB.wasPressed()) { return EVT_BTN_B_PRESSED; }

	/* 右のボタン（C）が押された */
	if (M5.BtnC.wasPressed()) { return EVT_BTN_C_PRESSED; }

	 /* 真ん中のボタン（B）が放された */
	if (M5.BtnB.wasReleased()) { return EVT_BTN_B_RELEASED; }

	/* 左のボタン（A）が1秒間長押しされた */
	if (M5.BtnA.pressedFor(SEC_LONG_PRESS)) { return EVT_BTN_A_LONG_PRESSED; }

	 /* 真ん中のボタン（B）が1秒間長押しされた */
	if (M5.BtnB.pressedFor(SEC_LONG_PRESS)) {return EVT_BTN_B_LONG_PRESSED; }	

	/* 右のボタン（C）が1秒間長押しされた */
	if (M5.BtnC.pressedFor(SEC_LONG_PRESS)) { return EVT_BTN_C_LONG_PRESSED; }

	/* タイマーが満了した */
	if (g_alarm_manager.is_expired()) { return EVT_TIME_EXPIRED; }

	return EVT_NONE;
}


/****************************************************************************
 * @brief 		通知されたイベントに応じてアプリケーションの動作状態を更新する
 * @param [in]	state - 現在の状態
 * @param [in]	event - 通知されたイベント
 * @return 		更新後の状態
 */
State_t change_state(State_t state, Event_t event)
{
	State_t state_new = STAT_UNKNOWN;

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
		/* 起動直後は STAT_IDLE へ遷移する */
		state_new = STAT_IDLE;
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
		updateFromFS(SD);   /* SDカードの "menu.bin" を読み込み */
		ESP.restart();      /* 再起動（もうこのプログラムには戻ってこない）*/
	}

	/* LCD（デバイス）の初期化 */
	lcd_real.init();
	lcd_real.setRotation(1);
	lcd_real.setBrightness(64);
	lcd_real.setColorDepth(16);  // RGB565の16ビットに設定

	/* バッファ画面（LCDと同サイズを2分割したもの）の初期化 */
	/* 1枚目のスプライトの初期化 */
	sprite1.setColorDepth(lcd_real.getColorDepth());
	sprite1.createSprite(lcd_real.width(), (lcd_real.height() / NUM_DIVIDE_OFFSCREEN));
	/* 2枚目のスプライトの初期化 */
	sprite2.setColorDepth(lcd_real.getColorDepth());
	sprite2.createSprite(lcd_real.width(), (lcd_real.height() / NUM_DIVIDE_OFFSCREEN));

	Serial.begin(115200);

	M5.Speaker.begin();		/* スピーカーから音を鳴らす */
	M5.Power.begin();		/* バッテリーの状態を取得する */

	/* プリセット時間の選択肢の登録（選択順） */
	g_time_selector.add(PresetTime(PresetTimeID::PRESET_TIME_2_MIN,  (2 * 60)));
	g_time_selector.add(PresetTime(PresetTimeID::PRESET_TIME_3_MIN,  (3 * 60)));
	g_time_selector.add(PresetTime(PresetTimeID::PRESET_TIME_5_MIN,  (5 * 60)));
	g_time_selector.add(PresetTime(PresetTimeID::PRESET_TIME_CUSTOM, (10 * 60), true));
}


/****************************************************************************
 * @brief 		Arduino関数：定常動作時の処理
 */
void loop() {
	static State_t state_prev = STAT_UNKNOWN;
	State_t state_next;
	Event_t event;

	/* ボタン入力の状態を更新する */
	M5.update();

	/* イベントを生成する（ボタン押し、タイマー満了、etc.） */
	event = generate_event();

	/* イベントに応じてアプリ動作状態を更新する */
	state_next = change_state(state_prev, event);

	/* 現在のアプリ動作状態を実行する
	 * （アプリ動作状態に変化があったら、新しい状態の初期化処理を走らせる） */
	bool is_changed = (state_prev != state_next);
	proc_state(state_next, is_changed);

	state_prev = state_next;
}
