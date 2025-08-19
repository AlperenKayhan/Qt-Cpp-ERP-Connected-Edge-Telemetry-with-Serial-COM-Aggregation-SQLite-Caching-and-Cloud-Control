#include "scatter3dwidget.h"
#include <algorithm>
#include <GL/gl.h> //OpenGL

Scatter3DWidget::Scatter3DWidget(QWidget *parent): QOpenGLWidget(parent)
    ,m_maxX(1.0f), m_maxY(1.0f), m_maxZ(1.0f)
    ,m_rotX(0.0f), m_rotY(0.0f)
    ,m_zoom(1.0f)
{}

void Scatter3DWidget::addPoint(float x, float y, float z)
{
    m_points.emplace_back(x, y, z);
    m_maxX = std::max(m_maxX, x);
    m_maxY = std::max(m_maxY, y);
    m_maxZ = std::max(m_maxZ, z);
    update();
}

void Scatter3DWidget::clearPoints()
{
    m_points.clear();
    m_maxX = m_maxY = m_maxZ = 1.0f;
    update();
}

void Scatter3DWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_POINT_SMOOTH);
    glPointSize(6.0f);
}

void Scatter3DWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
    m_proj.setToIdentity();
    m_proj.perspective(45.0f, float(w) / float(h ? h : 1), 0.1f, 1000.0f);
}

void Scatter3DWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Projection
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(m_proj.constData());

    // View (camera)
    m_view.setToIdentity();
    QVector3D baseEye(2.0f, 2.0f, 2.0f);
    QVector3D eye = baseEye * m_zoom;
    m_view.lookAt(eye, {0.5f, 0.5f, 0.5f}, {0,1,0});
    m_view.rotate(m_rotX, 1, 0, 0);
    m_view.rotate(m_rotY, 0, 1, 0);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m_view.constData());

    // Axes with ticks
    const float axisLen = 1.5f, tickSize = 0.02f;
    glBegin(GL_LINES);
    // X (red)
    glColor3f(1,0,0);
    glVertex3f(0,0,0); glVertex3f(axisLen,0,0);
    for(int i=1;i<=5;++i){
        float t = axisLen * i/5.0f;
        glVertex3f(t,-tickSize,0); glVertex3f(t,tickSize,0);
    }
    // Y (green)
    glColor3f(0,1,0);
    glVertex3f(0,0,0); glVertex3f(0,axisLen,0);
    for(int i=1;i<=5;++i){
        float t = axisLen * i/5.0f;
        glVertex3f(-tickSize,t,0); glVertex3f(tickSize,t,0);
    }
    // Z (blue)
    glColor3f(0,0,1);
    glVertex3f(0,0,0); glVertex3f(0,0,axisLen);
    for(int i=1;i<=5;++i){
        float t = axisLen * i/5.0f;
        glVertex3f(0,-tickSize,t); glVertex3f(0,tickSize,t);
    }
    glEnd();

    // Normalize & draw points (white)
    float sx = m_maxX>0?1.0f/m_maxX:1.0f;
    float sy = m_maxY>0?1.0f/m_maxY:1.0f;
    float sz = m_maxZ>0?1.0f/m_maxZ:1.0f;
    glColor3f(1,1,1);
    glBegin(GL_POINTS);
    for(const auto &p : m_points)
        glVertex3f(p.x()*sx, p.y()*sy, p.z()*sz);
    glEnd();
}

void Scatter3DWidget::mousePressEvent(QMouseEvent *event) {
    m_lastPos = event->pos();
}

void Scatter3DWidget::mouseMoveEvent(QMouseEvent *event) {
    int dx = event->x() - m_lastPos.x();
    int dy = event->y() - m_lastPos.y();
    m_rotX += dy;
    m_rotY += dx;
    m_lastPos = event->pos();
    update();
}

void Scatter3DWidget::wheelEvent(QWheelEvent *event) {
    float steps = event->angleDelta().y() / 120.0f;
    m_zoom *= (steps > 0 ? 0.9f : 1.1f);
    update();
}



