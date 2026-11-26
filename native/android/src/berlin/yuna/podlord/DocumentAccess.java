package berlin.yuna.podlord;

import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import java.io.OutputStream;

public final class DocumentAccess {
    private DocumentAccess() {}

    public static boolean retainRead(Context context, String source) {
        try {
            context.getContentResolver().takePersistableUriPermission(
                Uri.parse(source), Intent.FLAG_GRANT_READ_URI_PERMISSION);
            return true;
        } catch (SecurityException | IllegalArgumentException failure) {
            return false;
        }
    }

    public static boolean write(Context context, String destination, byte[] content) {
        try {
            Uri uri = Uri.parse(destination);
            if (!"content".equals(uri.getScheme())) return false;
            try (OutputStream output = context.getContentResolver().openOutputStream(uri, "wt")) {
                if (output == null) return false;
                output.write(content);
                output.flush();
                return true;
            }
        } catch (Exception failure) {
            return false;
        }
    }
}
