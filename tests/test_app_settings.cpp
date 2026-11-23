#include "AppSettings.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTest>

class TestAppSettings : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void clampsPetSize();
    void clampsSpeed();
    void persistsAndReloads();
    void validatesTimerDuration();

private:
    void clearSettings();
};

void TestAppSettings::clearSettings()
{
    QSettings settings(QStringLiteral("ChiikawaStudyPet"), QStringLiteral("DesktopPet"));
    settings.clear();
    settings.sync();
}

void TestAppSettings::initTestCase()
{
    clearSettings();
}

void TestAppSettings::clampsPetSize()
{
    AppSettings s;
    s.setPetSize(10);
    QCOMPARE(s.petSize(), AppSettings::kMinPetSize);
    s.setPetSize(9999);
    QCOMPARE(s.petSize(), AppSettings::kMaxPetSize);
}

void TestAppSettings::clampsSpeed()
{
    AppSettings s;
    s.setMovementSpeed(0.01);
    QVERIFY(s.movementSpeed() >= 0.25);
    s.setMovementSpeed(100.0);
    QVERIFY(s.movementSpeed() <= 3.0);
}

void TestAppSettings::persistsAndReloads()
{
    clearSettings();
    {
        AppSettings s;
        s.setPetSize(180);
        s.setMovementSpeed(1.5);
        s.setMovementEnabled(false);
        s.setAutoAnimationEnabled(false);
        s.setAlwaysOnTop(false);
        s.setLastPosition(QPoint(42, 84));
        s.setLastTimerSeconds(45 * 60);
        s.setSoundEnabled(false);
        s.save();
    }
    {
        AppSettings s;
        QCOMPARE(s.petSize(), 180);
        QCOMPARE(s.movementSpeed(), 1.5);
        QCOMPARE(s.movementEnabled(), false);
        QCOMPARE(s.autoAnimationEnabled(), false);
        QCOMPARE(s.alwaysOnTop(), false);
        QCOMPARE(s.lastPosition(), QPoint(42, 84));
        QVERIFY(s.hasSavedPosition());
        QCOMPARE(s.lastTimerSeconds(), 45 * 60);
        QCOMPARE(s.soundEnabled(), false);
    }
    clearSettings();
}

void TestAppSettings::validatesTimerDuration()
{
    AppSettings s;
    s.setLastTimerSeconds(0);
    QCOMPARE(s.lastTimerSeconds(), 1);
    s.setLastTimerSeconds(999999);
    QCOMPARE(s.lastTimerSeconds(), 24 * 60 * 60);
}

QTEST_MAIN(TestAppSettings)
#include "test_app_settings.moc"
