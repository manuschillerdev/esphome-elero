import java.nio.file.*;
import java.util.*;
import org.openhab.binding.elerotransmitterstick.internal.stick.*;
public class Replay {
  public static void main(String[] args) throws Exception {
    int observed=0, accepted=0;
    for (String line: Files.readAllLines(Path.of(args[0]))) {
      String[] cols=line.split("\t");
      byte[] tx=HexFormat.of().parseHex(cols[1]);
      byte[] rx=HexFormat.of().parseHex(cols[2]);
      CommandPacket cmd=new CommandPacket(Arrays.copyOf(tx,tx.length-1));
      Response r=ResponseUtil.createResponse(rx[3],rx[4],rx[5]);
      boolean match=r.isResponseFor(cmd);
      observed++; if(match) accepted++;
      System.out.println("{\"kind\":\"captured_reply\",\"case\":\""+cols[0]+"\",\"accepted\":"+match+"}");
    }
    CommandPacket cmd=new CommandPacket(HexFormat.of().parseHex("aa044e0001"));
    boolean correct=ResponseUtil.createResponse((byte)0,(byte)1,(byte)1).isResponseFor(cmd);
    boolean wrong=ResponseUtil.createResponse((byte)0,(byte)2,(byte)1).isResponseFor(cmd);
    boolean empty=ResponseUtil.createResponse((byte)0,(byte)0,(byte)0).isResponseFor(cmd);
    if(!correct || wrong || empty || accepted!=0) throw new AssertionError("Unexpected channel guard result");
    System.out.println("{\"kind\":\"guard_controls\",\"synthetic_correct_channel\":"+correct+",\"synthetic_wrong_channel\":"+wrong+",\"synthetic_zero_channel\":"+empty+",\"observed_replies\":"+observed+",\"accepted_observed_replies\":"+accepted+"}");
    Response sameChannel=ResponseUtil.createResponse((byte)0,(byte)1,(byte)1);
    boolean upMatch=sameChannel.isResponseFor(new CommandPacket(HexFormat.of().parseHex("aa054c000120")));
    boolean downMatch=sameChannel.isResponseFor(new CommandPacket(HexFormat.of().parseHex("aa054c000140")));
    if(!upMatch || !downMatch) throw new AssertionError("Channel-only matching changed");
    System.out.println("{\"kind\":\"correlation_limit\",\"synthetic_reply_matches_up\":"+upMatch+",\"synthetic_reply_matches_down\":"+downMatch+"}");
    for(int v: new int[]{0,1,2,3,4,5,6,7,8,9,10,11,13,14,15,16,17}) {
      ResponseStatus s=ResponseStatus.getFor((byte)v);
      System.out.println("{\"kind\":\"host_status_mapping\",\"usb_value\":"+v+",\"name\":\""+s+"\",\"percentage\":"+ResponseStatus.getPercentageFor(s)+"}");
    }
  }
}
