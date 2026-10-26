#pragma once
#include <QPointF>

// Account for already queued positions from before a cursor warp.
class CursorMotion
{
public:
    QPointF advance(QPointF position, bool &queuedBeforeWarp)
    {
        queuedBeforeWarp = false;
        if (!m_seen) { m_seen = true; m_last = position; return {}; }
        if (m_pending) {
            const auto oldDistance = position - m_old;
            const auto newDistance = position - m_last;
            if (lengthSquared(oldDistance) < lengthSquared(newDistance)) {
                queuedBeforeWarp = true;
                m_old = position;
                return oldDistance;
            }
            m_pending = false;
        }
        const auto delta = position - m_last;
        m_last = position;
        return delta;
    }
    void warped(QPointF original, QPointF target)
    {
        m_old = original; m_last = target; m_seen = m_pending = true;
    }
    void reset() { m_seen = m_pending = false; }
private:
    static double lengthSquared(QPointF p) { return p.x() * p.x() + p.y() * p.y(); }
    QPointF m_last, m_old;
    bool m_seen = false, m_pending = false;
};
