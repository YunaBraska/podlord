package berlin.yuna.podlord;

import android.content.Context;
import android.content.Intent;
import android.net.Uri;

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
}
