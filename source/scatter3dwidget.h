#ifndef SCATTER3DWIDGET_H
#define SCATTER3DWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QMatrix4x4>
#include <QVector3D>
#include <QMouseEvent>
#include <QWheelEvent>
#include <vector>

class Scatter3DWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit Scatter3DWidget(QWidget *parent = nullptr);

    // Add a 3D point and schedule repaint
    void addPoint(float x, float y, float z);

public slots:
    // Clear all points and reset scaling
    void clearPoints();

protected:
    // QOpenGLWidget overrides
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    // Interaction
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    std::vector<QVector3D> m_points;
    float m_maxX, m_maxY, m_maxZ;
    QMatrix4x4 m_proj, m_view;
    float m_rotX, m_rotY, m_zoom;
    QPoint m_lastPos;
};

#endif // SCATTER3DWIDGET_H
