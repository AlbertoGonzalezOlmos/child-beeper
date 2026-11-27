package org.childbeeper;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.TimePickerDialog;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.View;
import android.view.WindowInsets;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.TimeZone;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;

public final class MainActivity extends Activity {
    private EditText address, code, message;
    private TextView status, history, clock;
    private Button send, toggle, connect, find;
    private SharedPreferences preferences;
    private final ScheduledExecutorService io = Executors.newSingleThreadScheduledExecutor();
    private ScheduledFuture<?> polling;
    private volatile BeeperClient client;
    private volatile boolean visible;
    private long lastClockSync; // worker thread only
    private BeeperClient syncedClient; // worker thread only
    private JSONObject latest;
    private String renderedMessages = "";
    private String renderedBoot = "";
    private boolean sending;
    private int connectionAttempt;

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        setContentView(R.layout.activity_main);
        View page = findViewById(R.id.page);
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            page.setOnApplyWindowInsetsListener((view, insets) -> {
                android.graphics.Insets bars = insets.getInsets(
                        WindowInsets.Type.systemBars() | WindowInsets.Type.ime());
                view.setPadding(bars.left, bars.top, bars.right, bars.bottom);
                return insets;
            });
        }
        address = findViewById(R.id.address);
        code = findViewById(R.id.pair_code);
        message = findViewById(R.id.message);
        status = findViewById(R.id.status);
        history = findViewById(R.id.history);
        clock = findViewById(R.id.clock);
        send = findViewById(R.id.send);
        toggle = findViewById(R.id.toggle_alarm);
        connect = findViewById(R.id.connect);
        find = findViewById(R.id.find);
        preferences = getSharedPreferences("beeper", MODE_PRIVATE);
        address.setText(preferences.getString("address", ""));
        code.setText(preferences.getString("code", ""));
        if (saved == null) message.setText(preferences.getString("draft", ""));
        String host = preferences.getString("address", "");
        String key = preferences.getString("code", "");
        if (BeeperClient.validAddress(host) && key.matches("[0-9]{6}"))
            client = new BeeperClient(this, host, key);
        connect.setOnClickListener(v -> connect());
        find.setOnClickListener(v -> discover());
        send.setOnClickListener(v -> send());
        findViewById(R.id.set_alarm).setOnClickListener(v -> chooseAlarm());
        toggle.setOnClickListener(v -> {
            if (!ready()) return;
            JSONObject alarm = latest.optJSONObject("alarm");
            if (alarm != null) saveAlarm(!alarm.optBoolean("enabled"),
                    alarm.optInt("hour"), alarm.optInt("minute"));
        });
        findViewById(R.id.dismiss).setOnClickListener(v -> {
            if (ready()) write("/api/ack", new JSONObject(), null);
        });
    }

    @Override protected void onStart() {
        super.onStart();
        visible = true;
        polling = io.scheduleWithFixedDelay(this::poll, 0, 1, TimeUnit.SECONDS);
    }

    @Override protected void onStop() {
        visible = false;
        if (polling != null) polling.cancel(false);
        preferences.edit().putString("draft", message.getText().toString()).apply();
        super.onStop();
    }

    @Override protected void onDestroy() {
        io.shutdownNow();
        super.onDestroy();
    }

    private JSONObject clockBody() throws Exception {
        long now = System.currentTimeMillis();
        return new JSONObject().put("epoch", now / 1000)
                .put("utc_offset_minutes", TimeZone.getDefault().getOffset(now) / 60000);
    }

    private void poll() {
        BeeperClient target = client;
        if (!visible || target == null) return;
        try {
            if (syncedClient != target || SystemClock.elapsedRealtime() - lastClockSync > 60000) {
                target.request("/api/clock", clockBody());
                lastClockSync = SystemClock.elapsedRealtime();
                syncedClient = target;
            }
            JSONObject state = target.request("/api/state", null);
            ui(target, () -> render(state));
        } catch (Exception error) {
            ui(target, () -> {
                status.setText(error instanceof BeeperClient.PairingException ?
                        R.string.pairing_error : R.string.disconnected);
                if (error instanceof BeeperClient.PairingException) client = null;
            });
        }
    }

    private void ui(BeeperClient target, Runnable action) {
        runOnUiThread(() -> {
            if (!isDestroyed() && visible && client == target) action.run();
        });
    }

    private void connect() {
        String host = address.getText().toString().trim();
        String key = code.getText().toString().trim();
        if (!BeeperClient.validAddress(host) || !key.matches("[0-9]{6}")) {
            status.setText(R.string.invalid_connection);
            return;
        }
        int attempt = ++connectionAttempt;
        BeeperClient candidate = new BeeperClient(this, host, key);
        client = null;
        latest = null;
        connect.setEnabled(false);
        status.setText(R.string.connecting);
        io.execute(() -> {
            try {
                JSONObject state = candidate.request("/api/clock", clockBody());
                runOnUiThread(() -> {
                    if (isDestroyed() || attempt != connectionAttempt) return;
                    connect.setEnabled(true);
                    client = candidate;
                    preferences.edit().putString("address", host).putString("code", key).apply();
                    renderedMessages = "";
                    render(state);
                    getSystemService(InputMethodManager.class).hideSoftInputFromWindow(code.getWindowToken(), 0);
                });
            } catch (Exception error) {
                runOnUiThread(() -> {
                    if (isDestroyed() || attempt != connectionAttempt) return;
                    connect.setEnabled(true);
                    showError(error);
                });
            }
        });
    }

    private void discover() {
        find.setEnabled(false);
        status.setText(R.string.finding);
        io.execute(() -> {
            try {
                List<String> found = BeeperClient.discover(this);
                runOnUiThread(() -> {
                    if (isDestroyed()) return;
                    find.setEnabled(true);
                    if (found.isEmpty()) status.setText(R.string.none_found);
                    else if (found.size() == 1) selectAddress(found.get(0));
                    else if (visible) new AlertDialog.Builder(this).setTitle(R.string.choose_beeper)
                            .setItems(found.toArray(new String[0]), (dialog, which) -> selectAddress(found.get(which)))
                            .show();
                });
            } catch (Exception error) {
                runOnUiThread(() -> {
                    if (isDestroyed()) return;
                    find.setEnabled(true);
                    showError(error);
                });
            }
        });
    }

    private void selectAddress(String host) {
        address.setText(host);
        status.setText(R.string.found);
        code.requestFocus();
    }

    private boolean ready() {
        if (client != null && latest != null) return true;
        Toast.makeText(this, R.string.connect_first, Toast.LENGTH_SHORT).show();
        return false;
    }

    private void send() {
        if (!ready() || sending) return;
        String text = message.getText().toString().trim();
        if (text.isEmpty() || text.getBytes(StandardCharsets.UTF_8).length > 240) {
            message.setError(getString(R.string.message_size));
            return;
        }
        try {
            sending = true;
            send.setEnabled(false);
            write("/api/messages", new JSONObject().put("text", text), () -> {
                if (message.getText().toString().trim().equals(text)) message.setText("");
                preferences.edit().remove("draft").apply();
                Toast.makeText(this, R.string.sent, Toast.LENGTH_SHORT).show();
            });
        } catch (Exception error) {
            sending = false;
            send.setEnabled(true);
            showError(error);
        }
    }

    private void chooseAlarm() {
        if (!ready()) return;
        JSONObject alarm = latest.optJSONObject("alarm");
        new TimePickerDialog(this, (picker, hour, minute) -> saveAlarm(true, hour, minute),
                alarm == null ? 7 : alarm.optInt("hour"),
                alarm == null ? 0 : alarm.optInt("minute"), true).show();
    }

    private void saveAlarm(boolean enabled, int hour, int minute) {
        try {
            write("/api/alarm", new JSONObject().put("enabled", enabled)
                    .put("hour", hour).put("minute", minute),
                    () -> Toast.makeText(this, R.string.saved, Toast.LENGTH_SHORT).show());
        } catch (Exception error) { showError(error); }
    }

    private void write(String path, JSONObject body, Runnable success) {
        BeeperClient target = client;
        if (target == null) return;
        io.execute(() -> {
            JSONObject result = null;
            Exception failure = null;
            try { result = target.request(path, body); }
            catch (Exception error) { failure = error; }
            JSONObject state = result;
            Exception error = failure;
            runOnUiThread(() -> {
                if (isDestroyed()) return;
                sending = false;
                send.setEnabled(true);
                if (client != target) return;
                if (error == null) {
                    render(state);
                    if (success != null) success.run();
                } else if (path.equals("/api/messages") && !(error instanceof BeeperClient.PairingException)) {
                    status.setText(R.string.not_confirmed);
                    Toast.makeText(this, R.string.not_confirmed, Toast.LENGTH_LONG).show();
                } else showError(error);
            });
        });
    }

    private void render(JSONObject state) {
        latest = state;
        if (client != null) status.setText(getString(R.string.connected, client.address));
        JSONArray messages = state.optJSONArray("messages");
        String signature = messages == null ? "" : messages.toString();
        String boot = state.optString("boot_id");
        if (!signature.equals(renderedMessages) || !boot.equals(renderedBoot)) {
            renderedMessages = signature;
            renderedBoot = boot;
            StringBuilder text = new StringBuilder();
            if (messages != null) for (int i = 0; i < messages.length(); ++i) {
                JSONObject item = messages.optJSONObject(i);
                if (item == null) continue;
                boolean fromDevice = item.optString("from").equals("device");
                text.append(getString(fromDevice ? R.string.beeper : R.string.you));
                long epoch = item.optLong("epoch");
                if (epoch > 0) text.append(" · ").append(new SimpleDateFormat("HH:mm", Locale.getDefault())
                        .format(new Date(epoch * 1000)));
                text.append("\n").append(item.optString("text")).append("\n\n");
            }
            history.setText(text.length() == 0 ? getString(R.string.empty_messages) : text.toString().trim());
        }
        JSONObject alarm = state.optJSONObject("alarm");
        if (alarm != null) {
            boolean enabled = alarm.optBoolean("enabled");
            toggle.setText(enabled ? R.string.disable_alarm : R.string.enable_alarm);
            SimpleDateFormat format = new SimpleDateFormat("HH:mm:ss", Locale.getDefault());
            format.setTimeZone(new java.util.SimpleTimeZone(state.optInt("utc_offset_minutes") * 60000, "Beeper"));
            String time = state.optLong("epoch") > 0 ? format.format(new Date(state.optLong("epoch") * 1000)) : "--:--";
            clock.setText(getString(R.string.clock_status, time,
                    String.format(Locale.getDefault(), "%02d:%02d", alarm.optInt("hour"), alarm.optInt("minute")),
                    getString(alarm.optBoolean("ringing") ? R.string.alarm_ringing :
                            enabled ? R.string.alarm_on : R.string.alarm_off)));
        }
    }

    private void showError(Exception error) {
        status.setText(error instanceof BeeperClient.PairingException ? getString(R.string.pairing_error) :
                getString(R.string.error, error.getMessage()));
    }
}
