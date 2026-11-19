document.querySelectorAll('[data-count-demo]').forEach((demo) => {
  const clusters = demo.querySelector('[name=clusters]');
  const instances = demo.querySelector('[name=instances]');
  const update = () => {
    const c = Number(clusters.value);
    const n = Number(instances.value);
    const values = { clusters: c, instances: n, templates: c, clas: c * n, blas: n };
    for (const [key, value] of Object.entries(values)) {
      demo.querySelectorAll(`[data-value=${key}]`).forEach((item) => {
        item.textContent = value.toLocaleString('zh-CN');
      });
    }
  };
  clusters.addEventListener('input', update);
  instances.addEventListener('input', update);
  update();
});
document.querySelectorAll('[data-quiz]').forEach((quiz) => {
  const feedback = quiz.querySelector('.feedback');
  quiz.querySelectorAll('input[type=radio]').forEach((input) => {
    input.addEventListener('change', () => {
      const correct = input.value === quiz.dataset.answer;
      feedback.dataset.correct = String(correct);
      feedback.textContent = (correct ? '正确。' : '再想一步。') + quiz.dataset.explanation;
    });
  });
});
