package org.openhab.core.util; public class HexUtils { public static String bytesToHex(byte[] data) { return java.util.HexFormat.of().formatHex(data); } }
