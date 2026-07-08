package com.amap.agenui.render.layout;

import android.content.Context;
import android.graphics.Canvas;
import android.view.View;
import android.widget.FrameLayout;

import com.amap.agenui.render.drawable.ShadowPainter;

/**
 * A {@link FrameLayout} subclass that paints a software bitmap drop-shadow behind each
 * child view via {@link ShadowPainter}.
 *
 * <p>Used as the item shell for {@code ListComponent}s backed by a {@code RecyclerView}.
 */
public class ShadowFrameLayout extends FrameLayout {

    public ShadowFrameLayout(Context context) {
        super(context);
        setClipChildren(false);
        setClipToPadding(false);
    }

    @Override
    public void onViewRemoved(View child) {
        super.onViewRemoved(child);
        ShadowPainter.clearBitmap(child);
    }

    @Override
    protected boolean drawChild(Canvas canvas, View child, long drawingTime) {
        ShadowPainter.drawIfNeeded(canvas, child);
        return super.drawChild(canvas, child, drawingTime);
    }
}
