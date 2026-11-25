#include "AnimationManager.h"

#include <QLabel>
#include <QSignalSpy>
#include <QTest>

class TestAnimationManager : public QObject {
    Q_OBJECT

private slots:
    void loadsAllAssets();
    void switchesAnimation();
    void playRandomAvoidsCurrent();
    void resourcePathsExist();
    void hdScalingPreservesAspectAndDpr();
};

void TestAnimationManager::resourcePathsExist()
{
    for (int i = 0; i < static_cast<int>(AnimationId::Count); ++i) {
        const auto id = static_cast<AnimationId>(i);
        const QString path = AnimationManager::resourcePath(id);
        QVERIFY(!path.isEmpty());
        QVERIFY(path.startsWith(QStringLiteral(":/")));
    }
}

void TestAnimationManager::loadsAllAssets()
{
    QLabel label;
    AnimationManager manager(&label);
    QVERIFY(manager.loadAll());
    QVERIFY(manager.isReady());
    QCOMPARE(manager.currentAnimation(), AnimationId::Skip);
}

void TestAnimationManager::switchesAnimation()
{
    QLabel label;
    AnimationManager manager(&label);
    QVERIFY(manager.loadAll());

    QSignalSpy spy(&manager, &AnimationManager::animationChanged);
    manager.play(AnimationId::Twirl);
    QCOMPARE(manager.currentAnimation(), AnimationId::Twirl);
    QVERIFY(spy.count() >= 1);
}

void TestAnimationManager::playRandomAvoidsCurrent()
{
    QLabel label;
    AnimationManager manager(&label);
    QVERIFY(manager.loadAll());
    manager.play(AnimationId::Stomp);

    // Statistical check: many random picks should not all equal current
    int same = 0;
    for (int i = 0; i < 20; ++i) {
        const auto before = manager.currentAnimation();
        manager.playRandom(true);
        if (manager.currentAnimation() == before) {
            ++same;
        }
    }
    QVERIFY(same < 5);
}

void TestAnimationManager::hdScalingPreservesAspectAndDpr()
{
    QLabel label;
    AnimationManager manager(&label);
    QVERIFY(manager.loadAll());

    // Upscale beyond native 200px (stomp is 200x184) and verify non-square aspect ratio & HiDPI metadata
    manager.setTargetSize(256);
    manager.play(AnimationId::Stomp, false);
    const QPixmap stompPm = label.pixmap();
    QVERIFY(!stompPm.isNull());
    QVERIFY(stompPm.devicePixelRatio() >= 1.0);
    QVERIFY2(stompPm.width() > stompPm.height(),
             "Stomp sprite (200x184) must preserve non-square aspect ratio rather than stretching");

    const qreal logicalW = stompPm.width() / stompPm.devicePixelRatio();
    QVERIFY(logicalW >= 254.0 && logicalW <= 258.0);
}

QTEST_MAIN(TestAnimationManager)
#include "test_animation_manager.moc"
