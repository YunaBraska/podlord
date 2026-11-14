#include <QtCore/qglobal.h>
#import <UIKit/UIFontMetrics.h>

namespace podlord {
qreal appleTextSize(qreal base) {
    return [[UIFontMetrics metricsForTextStyle:UIFontTextStyleBody] scaledValueForValue:base];
}
}
