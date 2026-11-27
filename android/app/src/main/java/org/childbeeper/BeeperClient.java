package org.childbeeper;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.wifi.WifiManager;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.HttpURLConnection;
import java.net.InetAddress;
import java.net.SocketTimeoutException;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

/** Only talks to the explicitly paired IPv4 device, over the Wi-Fi network. */
final class BeeperClient {
    final String address;
    final String code;
    private final Context context;

    BeeperClient(Context context, String address, String code) {
        this.context = context.getApplicationContext();
        this.address = address;
        this.code = code;
    }

    static boolean validAddress(String address) {
        String[] parts = address.split("\\.", -1);
        if (parts.length != 4) return false;
        for (String part : parts) {
            if (!part.matches("[0-9]{1,3}") || Integer.parseInt(part) > 255) return false;
        }
        int first = Integer.parseInt(parts[0]);
        return first > 0 && first < 224 && first != 127;
    }

    private static Network wifiNetwork(Context context) throws IOException {
        ConnectivityManager manager = context.getSystemService(ConnectivityManager.class);
        for (Network network : manager.getAllNetworks()) {
            NetworkCapabilities caps = manager.getNetworkCapabilities(network);
            if (caps != null && caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) return network;
        }
        throw new IOException("Connect the phone to Wi-Fi first");
    }

    JSONObject request(String path, JSONObject body) throws Exception {
        URL url = new URL("http://" + address + path);
        HttpURLConnection connection = (HttpURLConnection) wifiNetwork(context).openConnection(url);
        connection.setConnectTimeout(3000);
        connection.setReadTimeout(3000);
        connection.setInstanceFollowRedirects(false); // never forward pairing codes to another host
        connection.setRequestProperty("X-Beeper-Key", code);
        connection.setRequestProperty("Connection", "close");
        try {
            if (body != null) {
                connection.setRequestMethod("POST");
                connection.setDoOutput(true);
                connection.setRequestProperty("Content-Type", "application/json");
                byte[] data = body.toString().getBytes(StandardCharsets.UTF_8);
                connection.setFixedLengthStreamingMode(data.length);
                try (var output = connection.getOutputStream()) { output.write(data); }
            }
            int status = connection.getResponseCode();
            if (status == 401) throw new PairingException();
            if (status == 400) throw new IOException(context.getString(R.string.invalid_message));
            if (status != 200) throw new IOException("Beeper HTTP " + status);
            try (InputStream stream = connection.getInputStream();
                 ByteArrayOutputStream buffer = new ByteArrayOutputStream()) {
                byte[] chunk = new byte[2048];
                int n;
                while ((n = stream.read(chunk)) != -1) {
                    if (buffer.size() + n > 32768) throw new IOException("Response too large");
                    buffer.write(chunk, 0, n);
                }
                JSONObject response = new JSONObject(buffer.toString(StandardCharsets.UTF_8.name()));
                if (response.optInt("protocol") != 1) throw new IOException("Unsupported beeper protocol");
                return response;
            }
        } finally {
            connection.disconnect();
        }
    }

    static List<String> discover(Context context) throws Exception {
        Network network = wifiNetwork(context);
        WifiManager wifi = context.getApplicationContext().getSystemService(WifiManager.class);
        WifiManager.MulticastLock multicast = wifi.createMulticastLock("child-beeper-discovery");
        multicast.acquire();
        Set<String> found = new LinkedHashSet<>();
        try (DatagramSocket socket = new DatagramSocket(null)) {
            socket.setReuseAddress(true);
            socket.bind(new java.net.InetSocketAddress(0));
            network.bindSocket(socket);
            socket.setBroadcast(true);
            socket.setSoTimeout(400);
            byte[] query = "CHILD_BEEPER_DISCOVER_V1".getBytes(StandardCharsets.US_ASCII);
            for (int attempt = 0; attempt < 3; ++attempt) {
                socket.send(new DatagramPacket(query, query.length,
                        InetAddress.getByName("255.255.255.255"), 42424));
                long until = android.os.SystemClock.elapsedRealtime() + 800;
                while (android.os.SystemClock.elapsedRealtime() < until) {
                    DatagramPacket reply = new DatagramPacket(new byte[512], 512);
                    try {
                        socket.receive(reply);
                        JSONObject info = new JSONObject(new String(reply.getData(), 0,
                                reply.getLength(), StandardCharsets.UTF_8));
                        if (info.optInt("protocol") == 1 && "Child Beeper".equals(info.optString("name")))
                            found.add(reply.getAddress().getHostAddress());
                    } catch (SocketTimeoutException ignored) {
                        // Keep listening until this attempt expires.
                    } catch (org.json.JSONException ignored) {
                        // An unrelated UDP packet is not a beeper.
                    }
                }
            }
        } finally {
            multicast.release();
        }
        return new ArrayList<>(found);
    }

    static final class PairingException extends IOException {}
}
