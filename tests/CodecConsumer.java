import dev.monaka.protocol.v1.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
public class CodecConsumer {
    public static void main(String[] args) throws Exception {
        DecodeResult result = MonakaCodec.INSTANCE.decodeEnvelope(Files.readAllBytes(Path.of(args[0])));
        if (result instanceof DecodeResult.Failure failure) {
            System.out.println("ERROR:" + failure.getCode()); return;
        }
        EncodeResult encoded = MonakaCodec.INSTANCE.encodeEnvelope(((DecodeResult.Success)result).getValue());
        if (!(encoded instanceof EncodeResult.Success success)) throw new IllegalStateException("encode failed");
        System.out.println(new String(success.getValue(),StandardCharsets.UTF_8));
    }
}
